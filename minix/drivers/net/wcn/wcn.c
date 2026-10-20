/*
 * wcn: the WCN3990 Wi-Fi of a Qualcomm SoC (the Mi Pad 4's SDM660) - the
 * service half (port/clover/hw-wifi.md).  The firmware runs in the modem
 * and is brought up over QMI by a user daemon; what needs the rights the
 * daemon has not is here, behind /dev/wlan0 (wcn_ioc.h).
 *
 * This first stage holds the MSA - the "wlan-msa-mem" range of
 * /reserved-memory the tree's "qcom,wcn3990-wifi" node names - and gives
 * the regions the firmware asks for to the modem and the WLAN through
 * TrustZone (MP/ASSIGN), as Linux's ath10k_qmi_setup_msa_permissions()
 * does.  The copy engines come next (wcn_ce.c).
 *
 * Memory given away must never be touched again: a read of it is an
 * external abort.  What is given is kept in DS ("wcn.msa"), so that a
 * restarted service knows it too and does not probe the range.
 */

#include <minix/drivers.h>
#include <minix/chardriver.h>
#include <minix/ds.h>
#include <minix/fdt.h>
#include <minix/log.h>
#include <minix/qcomscm.h>
#include <minix/vm.h>

#include <sys/mman.h>
#include <sys/ioccom.h>

#include <stdlib.h>
#include <string.h>

#include "wcn.h"
#include "wcn_ioc.h"

#define PAGE		4096
#define DS_MSA		"wcn.msa"

struct log wlog = {
	.name = "wcn",
	.log_level = LEVEL_INFO,
	.log_func = default_log
};

struct wcn_dt dt;

static volatile uint8_t *msa;		/* mapped, as a device */

/* Regions given away, as the firmware named them. */
static struct given {
	uint64_t addr;
	uint32_t size;
	uint32_t secure;
} given[WCN_REGIONS];
static unsigned ngiven;
static int tainted;		/* given away by an earlier run */

static uint32_t
prop_u32(const struct fdt_node *n, const char *name, unsigned i, uint32_t def)
{
	const uint8_t *p;
	unsigned len;

	if ((p = fdt_getprop(n, name, &len)) == NULL || len < 4 * (i + 1))
		return def;
	return (uint32_t)fdt_read_cells(p + 4 * i, 1);
}

static int
find_cb(void *cookie, int depth, const char *name, const struct fdt_node *n)
{
	const char *st;
	unsigned len;

	(void)cookie; (void)depth; (void)name;
	if (dt.found || !fdt_node_is_compatible(n, "qcom,wcn3990-wifi"))
		return 0;
	if ((st = fdt_getprop(n, "status", &len)) != NULL &&
	    strcmp(st, "okay") != 0 && strcmp(st, "ok") != 0)
		return 0;
	if (fdt_node_reg(n, 0, &dt.regs_pa, &dt.regs_sz) != 0)
		return 0;
	dt.msa_ph = prop_u32(n, "memory-region", 0, 0);
	/* Twelve lines, CE0..CE11 in order. */
	for (dt.nirq = 0; dt.nirq < WCN_CE_COUNT &&
	    (dt.irq[dt.nirq] = fdt_node_gic_irq(n, dt.nirq)) >= 0; dt.nirq++)
		;
	dt.found = 1;
	return 1;
}

/* The given regions, to DS and back: a bit pattern of what is out. */
static void
publish(void)
{
	uint32_t v = 0;
	unsigned i;

	/* Pages of the MSA given away, a bit per 64 KiB (1 MiB: 16 bits). */
	for (i = 0; i < ngiven; i++) {
		uint64_t a;

		for (a = given[i].addr; a < given[i].addr + given[i].size;
		    a += 65536)
			v |= 1u << ((a - dt.msa_pa) / 65536 % 32);
	}
	if (ds_publish_u32(DS_MSA, v, DSF_OVERWRITE) != OK)
		log_warn(&wlog, "cannot keep the MSA's state in DS\n");
}

static uint32_t
dst_list(int secure)
{
	uint32_t d;

	d = QCOMSCM_DST(QCOMSCM_VMID_MSS_MSA, QCOMSCM_PERM_RW);
	d |= QCOMSCM_DST(QCOMSCM_VMID_WLAN, QCOMSCM_PERM_RW) << 8;
	if (!secure)
		d |= QCOMSCM_DST(QCOMSCM_VMID_WLAN_CE, QCOMSCM_PERM_RW) << 16;
	return d;
}

static uint32_t
src_mask(int secure)
{

	return 1u << QCOMSCM_VMID_MSS_MSA | 1u << QCOMSCM_VMID_WLAN |
	    (secure ? 0 : 1u << QCOMSCM_VMID_WLAN_CE);
}

static int
assign(const struct wcn_region *r)
{
	struct qcomscm_answer a;
	volatile uint8_t *va;
	uint64_t o;
	unsigned i;
	int e;

	if (tainted)
		return EBUSY;
	if (ngiven >= WCN_REGIONS)
		return ENOSPC;
	/* Whole pages, inside the MSA, apart from what is out already. */
	if (r->size == 0 || ((r->addr | r->size) & (PAGE - 1)) != 0 ||
	    r->addr < dt.msa_pa || r->size > dt.msa_sz ||
	    r->addr - dt.msa_pa > dt.msa_sz - r->size)
		return EINVAL;
	for (i = 0; i < ngiven; i++)
		if (r->addr < given[i].addr + given[i].size &&
		    given[i].addr < r->addr + r->size)
			return EBUSY;
	va = msa + (r->addr - dt.msa_pa);
	/*
	 * The kernel finds the pages by this space's table, and a mapping of
	 * physical memory is filled on first touch: touch them now, while
	 * they are still ours.
	 */
	for (o = 0; o < r->size; o += PAGE)
		(void)*(volatile uint32_t *)(volatile void *)(va + o);
	e = sys_qcomscm_assign((void *)(uintptr_t)va, r->size,
	    1u << QCOMSCM_VMID_HLOS, dst_list(r->secure != 0), &a);
	if (e != OK || a.status != 0 || a.res[0] != 0) {
		log_warn(&wlog, "assign %llx+%x: call %d, err %ld, %lx\n",
		    (unsigned long long)r->addr, r->size, e, (long)a.status,
		    (unsigned long)a.res[0]);
		return e != OK ? e : EIO;
	}
	given[ngiven].addr = r->addr;
	given[ngiven].size = r->size;
	given[ngiven].secure = r->secure != 0;
	ngiven++;
	publish();
	log_info(&wlog, "MSA %llx+%x given to the modem and the WLAN%s\n",
	    (unsigned long long)r->addr, r->size,
	    r->secure ? " (secure)" : ", and the copy engines");
	return OK;
}

static int
reclaim(void)
{
	struct qcomscm_answer a;
	int e, bad = 0;

	if (tainted)
		return EBUSY;	/* what is out is not known in detail */
	while (ngiven > 0) {
		struct given *g = &given[ngiven - 1];

		e = sys_qcomscm_assign((void *)(uintptr_t)(msa + (g->addr -
		    dt.msa_pa)), g->size, src_mask(g->secure),
		    QCOMSCM_DST(QCOMSCM_VMID_HLOS, QCOMSCM_PERM_RW), &a);
		if (e != OK || a.status != 0 || a.res[0] != 0) {
			log_warn(&wlog, "take back %llx+%x: call %d, err %ld\n",
			    (unsigned long long)g->addr, g->size, e,
			    (long)a.status);
			bad = 1;
			break;
		}
		log_info(&wlog, "MSA %llx+%x taken back\n",
		    (unsigned long long)g->addr, g->size);
		ngiven--;
	}
	publish();
	return bad ? EIO : OK;
}

static int
w_open(devminor_t minor, int access __unused, endpoint_t user __unused)
{

	return minor == 0 ? OK : ENXIO;
}

static int
w_close(devminor_t minor __unused)
{

	return OK;
}

static int
w_ioctl(devminor_t minor, unsigned long request, endpoint_t endpt,
	cp_grant_id_t grant, int flags __unused, endpoint_t user __unused,
	cdev_id_t id __unused)
{
	struct wcn_msa m;
	struct wcn_region r;
	int e, on;

	if (minor != 0)
		return ENXIO;
	switch (request) {
	case WCNIOC_MSA:
		memset(&m, 0, sizeof(m));
		m.pa = dt.msa_pa;
		m.size = (uint32_t)dt.msa_sz;
		m.given = tainted ? ~0u : ngiven;
		return sys_safecopyto(endpt, grant, 0, (vir_bytes)&m,
		    sizeof(m));
	case WCNIOC_ASSIGN:
		if ((e = sys_safecopyfrom(endpt, grant, 0, (vir_bytes)&r,
		    sizeof(r))) != OK)
			return e;
		return assign(&r);
	case WCNIOC_RECLAIM:
		return reclaim();
	case WCNIOC_POWER:
		if ((e = sys_safecopyfrom(endpt, grant, 0, (vir_bytes)&on,
		    sizeof(on))) != OK)
			return e;
		return hw_power(on != 0);
	case WCNIOC_START:
		return hw_start();
	default:
		return ENOTTY;
	}
}

static struct chardriver w_tab = {
	.cdr_open	= w_open,
	.cdr_close	= w_close,
	.cdr_ioctl	= w_ioctl,
};

static int
w_init(int type __unused, sef_init_info_t *info __unused)
{
	uint32_t out;
	void *dtb;
	long lv;

	if (env_parse("log", "d", 0, &lv, LEVEL_NONE, LEVEL_TRACE) == EP_SET)
		wlog.log_level = (int)lv;
	if ((dtb = fdt_fetch()) == NULL)
		return ENXIO;
	(void)fdt_walk(dtb, find_cb, NULL);
	if (!dt.found) {
		free(dtb);
		log_info(&wlog, "no qcom,wcn3990-wifi in the tree\n");
		return ENXIO;
	}
	if (dt.msa_ph == 0 || fdt_phandle_reg(dtb, dt.msa_ph, 0, &dt.msa_pa,
	    &dt.msa_sz) != 0) {
		free(dtb);
		log_warn(&wlog, "the node names no MSA (memory-region)\n");
		return ENXIO;
	}
	free(dtb);
	if ((msa = vm_map_phys(SELF, (void *)(phys_bytes)dt.msa_pa,
	    (size_t)dt.msa_sz)) == MAP_FAILED) {
		log_warn(&wlog, "cannot map the MSA %llx+%llx; not granted by "
		    "RS?\n", (unsigned long long)dt.msa_pa,
		    (unsigned long long)dt.msa_sz);
		return EPERM;
	}
	/*
	 * A previous run of this service may have given parts away; then
	 * nothing here may touch them, and what it gave is not known in
	 * detail - refuse to give more until the modem is restarted.
	 */
	if (ds_retrieve_u32(DS_MSA, &out) == OK && out != 0) {
		log_warn(&wlog, "the MSA was given away before this start "
		    "(%x); left alone\n", out);
		tainted = 1;
	}
	log_info(&wlog, "WCN3990 at %llx, MSA %llx+%llx, %u lines\n",
	    (unsigned long long)dt.regs_pa, (unsigned long long)dt.msa_pa,
	    (unsigned long long)dt.msa_sz, dt.nirq);
	chardriver_announce();
	return OK;
}

int
main(int argc, char **argv)
{

	env_setargs(argc, argv);
	sef_setcb_init_fresh(w_init);
	sef_startup();
	for (;;) {
		message m;
		int st, r;

		if ((r = sef_receive_status(ANY, &m, &st)) != OK)
			panic("sef_receive: %d", r);
		if (is_ipc_notify(st) && _ENDPOINT_P(m.m_source) == HARDWARE) {
			hw_intr(m.m_notify.interrupts);
			continue;
		}
		chardriver_process(&w_tab, &m, st);
	}
	return 0;
}
