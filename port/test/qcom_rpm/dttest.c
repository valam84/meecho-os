/*
 * qcom_rpm's walk of the Mi Pad 4's real tree (sdm660-xiaomi-clover.dtb,
 * GPL-2.0-only and not kept here; the path is the first argument).
 *
 * rpm.c is included whole and rpm_init() runs against memory: the message
 * RAM has no table of contents, so it stops at glink_init() having found
 * and mapped everything - which is what this checks: the message RAM and
 * the IPC register at the tree's addresses, and exactly the regulators
 * Linux's regulator core would switch on at boot (always-on or boot-on in
 * the "qcom,rpm-pm660*-regulators" nodes), with "uv" only where the
 * tree's minimum and maximum are one value.
 */
#include <sys/mman.h>

#define main rpm_main
#include "rpm.c"
#undef main

int stub_log_level = 1;
static const char *dtb_path;
static unsigned checks, failures;

#define CHECK(c, ...) do {						\
	checks++;							\
	if (!(c)) {							\
		failures++;						\
		printf("  FAIL: ");					\
		printf(__VA_ARGS__);					\
		printf("\n");						\
	}								\
} while (0)

static uint8_t msgram[0x7000] __attribute__((aligned(8)));
static uint8_t apcsmem[0x1000] __attribute__((aligned(8)));
static unsigned maps_ram, maps_apcs, maps_other;

void *
vm_map_phys(int who, void *phys, size_t len)
{
	(void)who;
	if ((unsigned long)phys == 0x778000 && len == 0x7000) {
		maps_ram++;
		return msgram;
	}
	if ((unsigned long)phys == 0x17911000 && len == 0x1000) {
		maps_apcs++;
		return apcsmem;
	}
	printf("  FAIL: mapping 0x%lx+0x%zx\n", (unsigned long)phys, len);
	maps_other++;
	return MAP_FAILED;
}

int vm_unmap_phys(int who, void *vir, size_t len)
	{ (void)who; (void)vir; (void)len; return 0; }
void micro_delay(unsigned long us) { (void)us; }
void default_log(void) { }
int env_parse(const char *n, const char *f, int fl, long *p, long lo, long hi)
	{ (void)n; (void)f; (void)fl; (void)p; (void)lo; (void)hi; return 0; }
void env_setargs(int argc, char **argv) { (void)argc; (void)argv; }
void sef_setcb_init_fresh(int (*cb)(int, sef_init_info_t *)) { (void)cb; }
void sef_setcb_signal_handler(void (*cb)(int)) { (void)cb; }
void sef_startup(void) { }
int sef_receive_status(int src, message *m, int *st)
	{ (void)src; (void)m; (void)st; return 0; }

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

static int
has(const char *name, uint32_t type, uint32_t id, uint32_t uv)
{
	unsigned i;

	for (i = 0; i < dt.nregs; i++)
		if (strcmp(dt.regs[i].name, name) == 0)
			return dt.regs[i].type == type && dt.regs[i].id == id &&
			    dt.regs[i].uv == uv;
	return 0;
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
	memset(apcsmem, 0, sizeof(apcsmem));

	printf("qcom_rpm on the clover tree\n");
	r = rpm_init(0, NULL);
	CHECK(r == EINVAL, "rpm_init %d: expected EINVAL from an empty "
	    "message RAM", r);
	CHECK(maps_ram == 1 && maps_apcs == 1 && maps_other == 0,
	    "maps: ram %u apcs %u other %u", maps_ram, maps_apcs, maps_other);
	CHECK(dt.mbox_off == 8 && dt.mbox_bit == 0 && ipc_bit == 1,
	    "IPC offset %u bit %u", dt.mbox_off, dt.mbox_bit);
	for (i = 0; i < sizeof(apcsmem); i++)
		if (apcsmem[i] != 0)
			break;
	CHECK(i == sizeof(apcsmem), "the IPC register was written before "
	    "the link was up");
	for (i = 0; i < dt.nregs; i++)
		printf("  %s type %08x id %u uv %u\n", dt.regs[i].name,
		    dt.regs[i].type, dt.regs[i].id, dt.regs[i].uv);
	CHECK(has("l11a", RPM_TYPE_LDOA, 11, 0), "l11a (the touch screen's "
	    "1.8 V) missing or wrong");
	CHECK(has("l3b", RPM_TYPE_LDOB, 3, 3000000), "l3b at 3.0 V");
	CHECK(has("l13a", RPM_TYPE_LDOA, 13, 0), "l13a (boot-on)");
	CHECK(has("s4a", RPM_TYPE_SMPA, 4, 0), "s4a");
	CHECK(!has("l1a", RPM_TYPE_LDOA, 1, 0), "l1a: its always-on is "
	    "commented out in the tree");

	printf("\n%u checks, %u failures\n", checks, failures);
	return failures ? 1 : 0;
}
