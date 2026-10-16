/*
 * The pseudo-random generator of a Qualcomm SoC ("qcom,prng-ee": the
 * Mi Pad 4's SDM660, rng@793000), as a source for the pool beside the
 * RK356x TRNG (trng.c) and virtio-rng (virtio_rng.c).  Without one, sshd
 * makes its host key on the Mi Pad 4 from a pool fed by interrupt timing
 * alone - the state the CB2 was in before trng.c (port/clover/hw-net.md,
 * step 4).
 *
 * Linux drivers/crypto/qcom-rng.c: for the "-ee" variant nothing is set
 * up - the block belongs to the secure world, which configures it - and a
 * read is PRNG_STATUS bit 0 (data available), then PRNG_DATA_OUT, 32 bits.
 * Its one clock is GCC_PRNG_AHB_CLK, a vote shared with other masters;
 * before the first read it is looked at through the GCC (qcom_soc), and
 * voted for if it is off, as Linux enables it: on this SoC a read of an
 * unclocked block can stall the bus.
 *
 * What this file does not claim: the quality of the numbers.  They go to
 * the pool as a source, as trng.c's do.
 */

#include <minix/drivers.h>
#include <minix/fdt.h>
#include <minix/log.h>
#include <minix/syslib.h>
#include <minix/sysutil.h>
#include <minix/type.h>

#include <sys/mman.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "random.h"
#include "qcom_prng.h"

#if defined(__aarch64__)

#include "qcom_soc.h"

static struct log log = {
	.name = "qprng",
	.log_level = LEVEL_INFO,
	.log_func = default_log
};

#define PRNG_DATA_OUT		0x0000
#define PRNG_STATUS		0x0004
#define PRNG_STATUS_DATA_AVAIL	0x1
#define PRNG_MIN_SIZE		0x8
#define PRNG_FEED_WORDS		8		/* 256 bits a time */
#define PRNG_POLL_MAX		10000

static vir_bytes regs;

struct find {
	int found;
	u64_t base, size;
	uint32_t clk_prov, clk_id;
};

static int
find_prng(void *cookie, int UNUSED(depth), const char *UNUSED(name),
	const struct fdt_node *node)
{
	struct find *f = cookie;
	const uint8_t *c;
	unsigned len;

	if (!fdt_node_is_compatible(node, "qcom,prng-ee") ||
	    fdt_node_reg(node, 0, &f->base, &f->size) != 0 ||
	    f->size < PRNG_MIN_SIZE)
		return 0;
	if ((c = fdt_getprop(node, "clocks", &len)) != NULL && len == 8) {
		f->clk_prov = (uint32_t)fdt_read_cells(c, 1);
		f->clk_id = (uint32_t)fdt_read_cells(c + 4, 1);
	}
	f->found = 1;
	return 1;
}

static int
prng_word(u32_t *w)
{
	unsigned i;

	for (i = 0; i < PRNG_POLL_MAX; i++)
		if (*(volatile u32_t *)(regs + PRNG_STATUS) &
		    PRNG_STATUS_DATA_AVAIL) {
			*w = *(volatile u32_t *)(regs + PRNG_DATA_OUT);
			return OK;
		}
	return ETIMEDOUT;
}

size_t
qprng_feed(void)
{
	u32_t w[PRNG_FEED_WORDS];
	unsigned n;

	if (regs == 0)
		return 0;
	for (n = 0; n < PRNG_FEED_WORDS; n++)
		if (prng_word(&w[n]) != OK)
			break;
	if (n == 0)
		return 0;
	random_putbytes(w, n * sizeof(w[0]));
	return n * sizeof(w[0]);
}

int
qprng_init(void)
{
	struct find f;
	const char *cname = "?";
	void *dtb, *v;
	uint32_t cbcr = 0;
	int r, st;

	if ((dtb = fdt_fetch()) == NULL)
		return ENODEV;
	memset(&f, 0, sizeof(f));
	(void)fdt_walk(dtb, find_prng, &f);
	if (!f.found) {
		free(dtb);
		return ENODEV;
	}
	/* The clock, through the GCC the tree points at. */
	r = qsoc_init_gcc(dtb);
	free(dtb);
	if (r != OK || f.clk_prov == 0 || f.clk_prov != qsoc_gcc_phandle()) {
		log_warn(&log, "rng at 0x%llx: its clock is not one of the GCC "
		    "this driver knows (%d); not reading it\n",
		    (unsigned long long)f.base, r);
		return ENODEV;
	}
	if ((st = qsoc_clk_on(f.clk_id, &cbcr, &cname)) == 0) {
		log_info(&log, "%s is off (0x%08x); voting for it\n", cname,
		    cbcr);
		if ((r = qsoc_clk_enable(f.clk_id, 0)) != OK) {
			log_warn(&log, "%s would not start: %d\n", cname, r);
			return ENODEV;
		}
	} else if (st != 1) {
		log_warn(&log, "clock %u is not in the GCC's table\n",
		    f.clk_id);
		return ENODEV;
	}
	if ((v = vm_map_phys(SELF, (void *)(phys_bytes)f.base,
	    (size_t)f.size)) == MAP_FAILED) {
		log_warn(&log, "cannot map the rng at 0x%llx; not granted by "
		    "RS?\n", (unsigned long long)f.base);
		return ENODEV;
	}
	regs = (vir_bytes)v;
	if (qprng_feed() == 0) {
		log_warn(&log, "rng at 0x%llx gives nothing; not using it\n",
		    (unsigned long long)f.base);
		regs = 0;
		return ENODEV;
	}
	log_info(&log, "qcom,prng-ee at 0x%llx, %s on: a source for the pool\n",
	    (unsigned long long)f.base, cname);
	return OK;
}

#else /* !__aarch64__ */

int
qprng_init(void)
{
	return ENODEV;
}

size_t
qprng_feed(void)
{
	return 0;
}

#endif
