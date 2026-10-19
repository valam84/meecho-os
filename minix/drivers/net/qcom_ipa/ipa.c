/*
 * qcom_ipa: the IPA v2.6L of a Qualcomm SoC (the Mi Pad 4's SDM660), the
 * modem's data path to the AP (port/clover/hw-modem.md, section 9).
 *
 * Arguments (minix-service -args):
 *	stage=probe	read only, write nothing (the default)
 *	stage=init	route the IPA's streams through its SMMU bank, reset
 *			the IPA, set up its BAM pipes and fill its SRAM, then
 *			watch for the microcontroller the modem loads
 *	stage=loop	as init, with the loop rules (ipa2-lite's test_mode):
 *			then a packet each way through the IPA, no modem
 *	smmucheck=1	read the SMMU bank's registers back (only once a
 *			probe with read=banks found that safe)
 *	read=LIST	what the probe reads, of smmu (the apps SMMU's
 *			global registers and stream matches), ipa (the
 *			IPA's registers, with its clock voted) and banks
 *			(the SMMU's context banks); smmu,ipa by default
 *	log=N		0..5, 3 (info) by default
 */

#include "ipa.h"

#include <minix/chardriver.h>
#include <minix/ds.h>
#include <minix/fdt.h>
#include <minix/netdriver.h>
#include <minix/qcom_ipa.h>
#include <minix/qcom_rpm.h>

#include <stdlib.h>
#include <string.h>

struct ipa ipa;
struct log ilog = {
	.name = "ipa",
	.log_level = LEVEL_INFO,
	.log_func = default_log
};

/* The IPA's clock (the RPM's "ipa" resource), in kHz; 0 lets it go. */
int
ipa_rpm_clock(uint32_t khz)
{
	static endpoint_t ep = NONE;
	message m;
	int r;

	if (ep == NONE && ds_retrieve_label_endpt("qcom_rpm", &ep) != OK) {
		ep = NONE;
		return ENODEV;
	}
	memset(&m, 0, sizeof(m));
	m.m_type = QRPM_SET;
	m.m_u32.data[QRPM_TYPE] = QRPM_RES_IPA_CLK;
	m.m_u32.data[QRPM_ID] = 0;
	m.m_u32.data[QRPM_KEY] = QRPM_KEY_KHZ;
	m.m_u32.data[QRPM_VALUE] = khz;
	if ((r = ipc_sendrec(ep, &m)) != OK) {
		ep = NONE;
		return r;
	}
	return m.m_type;
}

/* Whether the comma-separated list names the word (libminc: no strstr). */
static int
has(const char *list, const char *word)
{
	size_t n = strlen(word);

	while (*list != '\0') {
		if (strncmp(list, word, n) == 0 &&
		    (list[n] == ',' || list[n] == '\0'))
			return 1;
		while (*list != '\0' && *list++ != ',')
			;
	}
	return 0;
}

static int stage_init;
static int hw_ok;

/*
 * /dev/ipa, for the QMI daemon (<minix/qcom_ipa.h>): what the modem is
 * told in INIT_DRIVER, the state, and the handshake's end.
 */
static int modem_ready;

static int
c_open(devminor_t minor, int UNUSED(access), endpoint_t UNUSED(user))
{
	return minor == 0 ? OK : ENXIO;
}

static int
c_close(devminor_t UNUSED(minor))
{
	return OK;
}

static int
c_ioctl(devminor_t UNUSED(minor), unsigned long req, endpoint_t ep,
	cp_grant_id_t grant, int UNUSED(flags), endpoint_t UNUSED(user),
	cdev_id_t UNUSED(id))
{
	struct ipa_info in;
	int v, r;

	switch (req) {
	case IPAIOC_INFO:
		memset(&in, 0, sizeof(in));
		if (hw.part[MEM_MDM].size != 0)
			in.state |= IPA_ST_SRAM;
		if (hw.uc_loaded)
			in.state |= IPA_ST_UC;
		if (modem_ready)
			in.state |= IPA_ST_MODEM;
		in.hdr_start = hw.part[MEM_MDM_HDR].offset;
		in.hdr_end = hw.part[MEM_MDM_HDR].offset +
		    hw.part[MEM_MDM_HDR].size - 1;
		in.rt4_start = hw.part[MEM_RT_V4].offset;
		in.rt4_end = hw.part[MEM_RT_V4].size / 4 - 1;
		in.rt6_start = hw.part[MEM_RT_V6].offset;
		in.rt6_end = hw.part[MEM_RT_V6].size / 4 - 1;
		in.ft4_start = hw.part[MEM_FT_V4].offset;
		in.ft6_start = hw.part[MEM_FT_V6].offset;
		in.mdm_start = hw.part[MEM_MDM].offset;
		in.mdm_size = hw.part[MEM_MDM].size;
		in.comp_start = hw.part[MEM_MDM_COMP].offset;
		in.comp_end = hw.part[MEM_MDM_COMP].offset +
		    hw.part[MEM_MDM_COMP].size - 1;
		return sys_safecopyto(ep, grant, 0, (vir_bytes)&in,
		    sizeof(in));
	case IPAIOC_MODEM:
		if ((r = sys_safecopyfrom(ep, grant, 0, (vir_bytes)&v,
		    sizeof(v))) != OK)
			return r;
		if (v && !modem_ready)
			log_info(&ilog, "modem handshake complete, data path "
			    "enabled\n");
		else if (!v && modem_ready)
			log_info(&ilog, "the modem's IPA driver is gone\n");
		modem_ready = v != 0;
		if (hw_ok)
			ipa_net_modem(modem_ready);
		return OK;
	default:
		return ENOTTY;
	}
}

static struct chardriver c_tab = {
	.cdr_open	= c_open,
	.cdr_close	= c_close,
	.cdr_ioctl	= c_ioctl,
};

/* The arguments; stage_init and ipa_test_mode from stage=. */
static int do_smmu = 1, do_ipa = 1, do_banks = 0;
static unsigned mux = 1;

static int
parse(void)
{
	char buf[16], what[32];
	long lv;

	if (env_parse("log", "d", 0, &lv, LEVEL_NONE, LEVEL_TRACE) == EP_SET)
		ilog.log_level = (int)lv;
	if (env_parse("smmucheck", "d", 0, &lv, 0, 1) == EP_SET)
		smmu_check = (int)lv;
	if (env_parse("mux", "d", 0, &lv, 1, 254) == EP_SET)
		mux = (unsigned)lv;
	if (env_get_param("stage", buf, sizeof(buf)) == OK) {
		if (strcmp(buf, "init") == 0)
			stage_init = 1;
		else if (strcmp(buf, "loop") == 0)
			stage_init = ipa_test_mode = 1;
		else if (strcmp(buf, "probe") != 0) {
			log_warn(&ilog, "stage=%s: probe, init or loop\n", buf);
			return EINVAL;
		}
	}
	if (env_get_param("read", what, sizeof(what)) == OK) {
		do_smmu = has(what, "smmu");
		do_ipa = has(what, "ipa");
		do_banks = has(what, "banks");
	}
	return OK;
}

static int
find(void)
{
	void *dtb;
	int r;

	if ((dtb = fdt_fetch()) == NULL)
		return ENXIO;
	r = ipa_find(dtb);
	free(dtb);
	return r;
}

/*===========================================================================*
 *				stage=probe				     *
 *===========================================================================*/
static int
probe_init(int UNUSED(type), sef_init_info_t *UNUSED(info))
{
	int r;

	if ((r = parse()) != OK || (r = find()) != OK)
		return r;
	if (do_smmu || do_banks)
		(void)ipa_probe_smmu(do_banks);
	if (do_ipa && ipa_probe_regs() == OK)
		(void)ipa_rpm_clock(0);
	log_info(&ilog, "stage=probe: nothing written\n");
	return OK;
}

/*===========================================================================*
 *				stage=init, stage=loop			     *
 *===========================================================================*/

static int
n_init(unsigned int UNUSED(instance), netdriver_addr_t *hwaddr,
	uint32_t *caps, unsigned int *ticks)
{
	int r;

	if ((r = find()) != OK)
		return r;
	if (ipa.smmu_sz == 0 || (ipa.smmu = ipa_map(ipa.smmu_pa,
	    ipa.smmu_sz, "the SMMU")) == NULL ||
	    (ipa.base = ipa_map(ipa.pa, ipa.sz, "the IPA")) == NULL)
		return ENXIO;
	/* Stays whatever happens: what it routed is the IPA's. */
	if ((r = ipa_hw_init()) != OK)
		log_warn(&ilog, "stage=init failed: %d\n", r);
	else if (ipa_test_mode)
		(void)ipa_loop_test();
	else if ((r = ipa_net_init(mux)) != OK)
		log_warn(&ilog, "no buffers for ipa0: %d\n", r);
	else
		hw_ok = 1;
	ipa_net_hwaddr(hwaddr);
	*caps = 0;
	*ticks = 1;
	chardriver_announce();
	return OK;
}

static void
n_tick(void)
{
	if (!hw_ok)
		return;
	(void)ipa_uc_poll();
	ipa_net_tick();
}

static void
n_other(const message *m, int ipc_status)
{
	message copy;

	if (IS_CDEV_RQ(m->m_type)) {
		copy = *m;	/* libchardriver's takes it writable */
		chardriver_process(&c_tab, &copy, ipc_status);
	}
}

static const struct netdriver n_tab = {
	.ndr_name	= "ipa",
	.ndr_init	= n_init,
	.ndr_recv	= ipa_net_recv,
	.ndr_send	= ipa_net_send,
	.ndr_get_link	= ipa_net_link,
	.ndr_tick	= n_tick,
	.ndr_other	= n_other,
};

int
main(int argc, char **argv)
{
	message m;
	int ipc_status;

	env_setargs(argc, argv);
	if (parse() != OK)
		return 1;
	if (stage_init) {
		/* An interface for lwip (ipa0), /dev/ipa for ipaqmid. */
		netdriver_task(&n_tab);
		return 0;
	}
	sef_setcb_init_fresh(probe_init);
	sef_startup();
	/* Nothing to serve; stay, so that RS does not start it again. */
	for (;;)
		(void)sef_receive_status(ANY, &m, &ipc_status);
	return 0;
}
