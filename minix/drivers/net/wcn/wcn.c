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

#include <stdio.h>
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

static netdriver_addr_t mac;		/* the station's address */

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
	const uint8_t *p;
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
	/* iommus: one SMMU's phandle and the stream ids, a cell each. */
	if ((p = fdt_getprop(n, "iommus", &len)) != NULL) {
		unsigned off;

		for (off = 0; off + 8 <= len && dt.nsid < 4; off += 8) {
			u32_t ph = (u32_t)fdt_read_cells(p + off, 1);

			if (dt.smmu_ph != 0 && ph != dt.smmu_ph)
				continue;
			dt.smmu_ph = ph;
			dt.sid[dt.nsid++] =
			    (uint32_t)fdt_read_cells(p + off + 4, 1);
		}
	}
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

static struct wcn_msg iomsg;

/* WMI and frames, both ways (wcn_ioc.h). */
static int
msg_ioctl(unsigned long request, endpoint_t endpt, cp_grant_id_t grant)
{
	ssize_t n;
	int e;

	if ((e = sys_safecopyfrom(endpt, grant, 0, (vir_bytes)&iomsg,
	    sizeof(iomsg))) != OK)
		return e;
	switch (request) {
	case WCNIOC_WMI_SEND:
		if (iomsg.len == 0 || iomsg.len > WCN_MSG_MAX || wmi_ep < 0)
			return EINVAL;
		return hw_wmi_send(iomsg.data, iomsg.len);
	case WCNIOC_WMI_RECV:
		if (wmi_ep < 0)
			return EINVAL;
		n = hw_wait((unsigned)wmi_ep, iomsg.data, sizeof(iomsg.data),
		    iomsg.ms > 10000 ? 10000 : iomsg.ms);
		if (n < 0)
			return (int)-n;
		iomsg.len = (uint32_t)n;
		break;
	case WCNIOC_FRAME:
		if (iomsg.len == 0 || iomsg.len > WCN_MSG_MAX)
			return EINVAL;
		if ((e = hw_frame(iomsg.data, iomsg.len, &iomsg.da)) != OK)
			return e;
		break;
	case WCNIOC_EAPOL_TX:
		if (iomsg.len < 14 || iomsg.len > WCN_MSG_MAX)
			return EINVAL;
		return data_eapol_send(iomsg.data, iomsg.len);
	case WCNIOC_EAPOL_RX:
		n = data_eapol_recv(iomsg.data, sizeof(iomsg.data),
		    iomsg.ms > 10000 ? 10000 : iomsg.ms);
		if (n < 0)
			return (int)-n;
		iomsg.len = (uint32_t)n;
		break;
	}
	return sys_safecopyto(endpt, grant, 0, (vir_bytes)&iomsg,
	    sizeof(iomsg));
}

static int
w_ioctl(devminor_t minor, unsigned long request, endpoint_t endpt,
	cp_grant_id_t grant, int flags __unused, endpoint_t user __unused,
	cdev_id_t id __unused)
{
	struct wcn_msa m;
	struct wcn_region r;
	struct wcn_link lk;
	struct wcn_mac wm;
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
	case WCNIOC_WMI_SEND:
	case WCNIOC_WMI_RECV:
	case WCNIOC_FRAME:
	case WCNIOC_EAPOL_TX:
	case WCNIOC_EAPOL_RX:
		return msg_ioctl(request, endpt, grant);
	case WCNIOC_HTT_START:
		return data_start();
	case WCNIOC_MAC:
		memset(&wm, 0, sizeof(wm));
		memcpy(wm.addr, mac.na_addr, 6);
		return sys_safecopyto(endpt, grant, 0, (vir_bytes)&wm,
		    sizeof(wm));
	case WCNIOC_LINK:
		if ((e = sys_safecopyfrom(endpt, grant, 0, (vir_bytes)&lk,
		    sizeof(lk))) != OK)
			return e;
		e = data_link(&lk);
		netdriver_link();
		return e;
	default:
		return ENOTTY;
	}
}

static struct chardriver w_tab = {
	.cdr_open	= w_open,
	.cdr_close	= w_close,
	.cdr_ioctl	= w_ioctl,
};

/* ---- the network side, to lwip ---- */


/*
 * The station's address.  The WLAN keeps none of its own (Linux on this
 * tablet: "invalid MAC address; choosing random"): mac= if given, else a
 * locally administered one made once per boot and kept in DS, so that a
 * restarted service is the same station.
 */
static void
pick_mac(void)
{
	char buf[32];
	unsigned v[6], i;
	uint32_t hi, lo;
	u64_t t;

	if (env_get_param("mac", buf, sizeof(buf)) == OK) {
		/* xx:xx:xx:xx:xx:xx (libminc has no sscanf) */
		const char *p = buf;
		char *end;

		for (i = 0; i < 6; i++) {
			v[i] = (unsigned)strtoul(p, &end, 16);
			if (end == p || v[i] > 0xff || (i < 5 && *end != ':'))
				break;
			p = end + 1;
		}
		if (i == 6) {
			for (i = 0; i < 6; i++)
				mac.na_addr[i] = v[i];
			return;
		}
		log_warn(&wlog, "mac=%s: not an address\n", buf);
	}
	if (ds_retrieve_u32("wcn.mac.hi", &hi) != OK ||
	    ds_retrieve_u32("wcn.mac.lo", &lo) != OK) {
		read_tsc_64(&t);
		hi = 0x0200 | (uint32_t)((t >> 8) & 0xfc);	/* local, unicast */
		lo = (uint32_t)(t ^ (t >> 32)) * 2654435761u;
		(void)ds_publish_u32("wcn.mac.hi", hi, DSF_OVERWRITE);
		(void)ds_publish_u32("wcn.mac.lo", lo, DSF_OVERWRITE);
	}
	mac.na_addr[0] = 0x02;
	mac.na_addr[1] = hi & 0xff;
	mac.na_addr[2] = lo >> 24;
	mac.na_addr[3] = lo >> 16;
	mac.na_addr[4] = lo >> 8;
	mac.na_addr[5] = lo;
}

static int
w_init(unsigned int instance __unused, netdriver_addr_t *hwaddr,
	uint32_t *caps, unsigned int *ticks)
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
	if (dt.smmu_ph == 0 || dt.nsid == 0 || fdt_phandle_reg(dtb,
	    dt.smmu_ph, 0, &dt.smmu_pa, &dt.smmu_sz) != 0) {
		free(dtb);
		log_warn(&wlog, "the node names no SMMU (iommus)\n");
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
	pick_mac();
	*hwaddr = mac;
	*caps = NDEV_CAP_BCAST | NDEV_CAP_MCAST;
	*ticks = 0;
	log_info(&wlog, "WCN3990 at %llx, MSA %llx+%llx, %u lines, station "
	    "%02x:%02x:%02x:%02x:%02x:%02x\n",
	    (unsigned long long)dt.regs_pa, (unsigned long long)dt.msa_pa,
	    (unsigned long long)dt.msa_sz, dt.nirq, mac.na_addr[0],
	    mac.na_addr[1], mac.na_addr[2], mac.na_addr[3], mac.na_addr[4],
	    mac.na_addr[5]);
	chardriver_announce();
	return OK;
}

static void
w_stop(void)
{

	hw_stop();
}

/* Frames: none until a network is joined (HTT and the station come next). */
static ssize_t
w_recv(struct netdriver_data *data, size_t max)
{

	return data_net_recv(data, max);
}

static int
w_send(struct netdriver_data *data, size_t size)
{

	return data_net_send(data, size);
}

static unsigned int
w_get_link(uint32_t *media __unused)
{

	return data_link_up() ? NDEV_LINK_UP : NDEV_LINK_DOWN;
}

static void
w_intr(unsigned int mask)
{

	hw_intr(mask);
}

/* /dev/wlan0's requests come here: everything the network side does not
 * know is the character device's. */
static void
w_other(const message *m, int ipc_status)
{

	message copy = *m;	/* libchardriver takes it writable */

	chardriver_process(&w_tab, &copy, ipc_status);
}

static const struct netdriver w_net = {
	.ndr_name	= "wlan",
	.ndr_init	= w_init,
	.ndr_stop	= w_stop,
	.ndr_recv	= w_recv,
	.ndr_send	= w_send,
	.ndr_get_link	= w_get_link,
	.ndr_intr	= w_intr,
	.ndr_other	= w_other,
};

int
main(int argc, char **argv)
{

	env_setargs(argc, argv);
	netdriver_task(&w_net);
	return 0;
}
