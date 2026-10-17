/*
 * The PMIC driver's arbiter discovery, run on the Mi Pad 4's real tree.
 *
 * pmic.c is included whole and arb_init() runs as on the tablet against
 * sdm660-xiaomi-clover.dtb (GPL-2.0-only, not kept in this tree; the path
 * is the first argument), read by libsys's fdt.c.  The arbiter's regions
 * are plain memory.  Checked: the four regions the driver needs are found
 * by name among the five the node lists ("intr" is not one of them) and
 * mapped at the tree's addresses and sizes; the execution environment is
 * the tree's qcom,ee; and an arbiter whose version register says neither
 * v2 nor v3 is left alone - refused before a single write.  Then the same
 * with the version of the real part (v3, 0x30000000 - hw-spmi.md): the
 * driver goes on to read the channel table, and still writes nothing.
 */
#include <sys/mman.h>

#define main pmic_main
#include "pmic.c"
#undef main

int stub_log_level = 1;
static const char *dtb_path;
static unsigned checks, failures;

#define CHECK(cond, ...) do {						\
	checks++;							\
	if (!(cond)) {							\
		failures++;						\
		printf("  FAIL: ");					\
		printf(__VA_ARGS__);					\
		printf("\n");						\
	}								\
} while (0)

static const struct { unsigned long pa; size_t sz; } want[4] = {
	{ 0x800f000, 0x1000 },		/* core */
	{ 0x8400000, 0x1000000 },	/* chnls */
	{ 0x9400000, 0x1000000 },	/* obsrvr */
	{ 0x800a000, 0x3000 },		/* cnfg */
};
static uint8_t *mem[4];
static unsigned mapped[4], mapped_other;

void *
vm_map_phys(int who, void *phys, size_t len)
{
	unsigned i;

	(void)who;
	for (i = 0; i < 4; i++)
		if ((unsigned long)phys == want[i].pa && len == want[i].sz) {
			mapped[i]++;
			return mem[i];
		}
	printf("  FAIL: mapping 0x%lx+0x%zx is none of the four regions\n",
	    (unsigned long)phys, len);
	mapped_other++;
	return MAP_FAILED;
}

int vm_unmap_phys(int who, void *vir, size_t len)
	{ (void)who; (void)vir; (void)len; return 0; }
void micro_delay(unsigned long us) { (void)us; }

void *
fdt_fetch(void)
{
	FILE *f = fopen(dtb_path, "rb");
	long n;
	void *buf;

	if (f == NULL)
		return NULL;
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	rewind(f);
	buf = malloc((size_t)n);
	if (buf != NULL && fread(buf, 1, (size_t)n, f) != (size_t)n) {
		free(buf);
		buf = NULL;
	}
	fclose(f);
	return buf;
}

void default_log(void) { }
int env_parse(const char *n, const char *f, int fl, long *p, long lo, long hi)
	{ (void)n; (void)f; (void)fl; (void)p; (void)lo; (void)hi; return 0; }
void env_setargs(int argc, char **argv) { (void)argc; (void)argv; }
void sef_setcb_init_fresh(int (*cb)(int, sef_init_info_t *)) { (void)cb; }
void sef_setcb_signal_handler(void (*cb)(int)) { (void)cb; }
void sef_startup(void) { }
int sys_setalarm(clock_t t, int a) { (void)t; (void)a; return 0; }
clock_t sys_hz(void) { return 100; }
void inputdriver_send_event(int m, unsigned short p, unsigned short c,
	int v, int f) { (void)m; (void)p; (void)c; (void)v; (void)f; }
void inputdriver_announce(unsigned int t) { (void)t; }
void inputdriver_terminate(void) { }
void inputdriver_task(struct inputdriver *t) { (void)t; }
int env_get_param(const char *k, char *v, int n)
	{ (void)k; (void)v; (void)n; return -1; }
int rmib_register(const int *name, unsigned int len, struct rmib_node *n)
	{ (void)name; (void)len; (void)n; return 0; }
int rmib_deregister(struct rmib_node *n) { (void)n; return 0; }
void rmib_process(const message *m, int s) { (void)m; (void)s; }
ssize_t rmib_readwrite(struct rmib_call *c, struct rmib_node *n,
	struct rmib_oldp *o, struct rmib_newp *w)
	{ (void)c; (void)n; (void)o; (void)w; return 0; }

static int
untouched(unsigned i, size_t skip_off)
{
	size_t off;

	for (off = 0; off < want[i].sz; off += 4)
		if (off != skip_off && *(uint32_t *)(mem[i] + off) != 0)
			return 0;
	return 1;
}

int
main(int argc, char **argv)
{
	unsigned i;
	int r;

	if (argc < 2) {
		fprintf(stderr, "usage: %s sdm660-xiaomi-clover.dtb\n", argv[0]);
		return 2;
	}
	dtb_path = argv[1];
	for (i = 0; i < 4; i++)
		if ((mem[i] = calloc(1, want[i].sz)) == NULL)
			return 2;

	printf("arbiter on the clover tree, version register 0\n");
	r = arb_init();
	CHECK(r != OK, "an arbiter of version 0 was taken");
	CHECK(mapped_other == 0, "a region not in the tree was mapped");
	for (i = 0; i < 4; i++)
		CHECK(mapped[i] == 1, "region %u mapped %u times", i,
		    mapped[i]);
	CHECK(spmi.ee == 0, "EE %u, the tree says 0", spmi.ee);
	for (i = 0; i < 4; i++)
		CHECK(untouched(i, (size_t)-1), "region %u written", i);

	printf("arbiter v3 (0x30000000), empty channel table\n");
	*(uint32_t *)(mem[0] + 0x0) = 0x30000000;	/* VERSION, core+0 */
	memset(mapped, 0, sizeof(mapped));
	r = arb_init();
	CHECK(r == OK, "v3 refused: %d (version read 0x%08x)", r,
	    spmi.version);
	CHECK(spmi.version == 0x30000000, "version 0x%08x", spmi.version);
	for (i = 0; i < 4; i++)
		CHECK(untouched(i, i == 0 ? 0x0 : (size_t)-1),
		    "region %u written", i);

	printf("\n%u checks, %u failures\n", checks, failures);
	return failures ? 1 : 0;
}
