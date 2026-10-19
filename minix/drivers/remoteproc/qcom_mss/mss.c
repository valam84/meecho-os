/*
 * qcom_mss: the modem of a Qualcomm SoC (the Mi Pad 4's SDM660), loaded and
 * started, then watched - Linux's qcom_q6v5_mss.c and its smp2p edge.
 *
 * The firmware is the tablet's own and is not in MEECHO's tree (Qualcomm's
 * and Xiaomi's licence): a copy of the modem partition's image/ directory
 * lies on the root in fwdir= (mba.mbn, modem.mdt and modem.bNN by the
 * tree's firmware-name).  TrustZone checks its signature; a file that is
 * not this tablet's is refused there, not here.
 *
 * Arguments (minix-service -args):
 *	fwdir=DIR	the firmware (default /libdata/firmware/qcom/clover/modem)
 *	stage=probe	map and read SMEM, the clocks and the reset; write nothing
 *	stage=mba	start the MBA and stop there (the modem reset again)
 *	stage=boot	the whole of it (the default)
 *	log=N		0..5, 3 (info) by default
 *
 * Once the modem is up the service stays: it releases the proxy votes on
 * the modem's "handover" and says when the modem reports a fatal error.
 * Stopping and restarting the modem are not written yet.
 * port/clover/hw-modem.md.
 */

#include "mss.h"
#include "qcom_soc.h"

#include <minix/fdt.h>

#include <stdlib.h>
#include <string.h>

struct mss mss;
struct log mlog = {
	.name = "mss",
	.log_level = LEVEL_INFO,
	.log_func = default_log
};

#define STAGE_PROBE	0
#define STAGE_MBA	1
#define STAGE_BOOT	2

static int stage = STAGE_BOOT;
static int up;

static int
mss_init(int UNUSED(type), sef_init_info_t *UNUSED(info))
{
	char buf[16];
	void *dtb;
	long lv;
	int r;

	if (env_parse("log", "d", 0, &lv, LEVEL_NONE, LEVEL_TRACE) == EP_SET)
		mlog.log_level = (int)lv;
	(void)env_get_param("fwdir", mss_fwdir, sizeof(mss_fwdir));
	if (env_get_param("stage", buf, sizeof(buf)) == OK) {
		if (strcmp(buf, "probe") == 0)
			stage = STAGE_PROBE;
		else if (strcmp(buf, "mba") == 0)
			stage = STAGE_MBA;
		else if (strcmp(buf, "boot") == 0)
			stage = STAGE_BOOT;
		else {
			log_warn(&mlog, "stage=%s: probe, mba or boot\n", buf);
			return EINVAL;
		}
	}

	if ((dtb = fdt_fetch()) == NULL)
		return ENXIO;
	if ((r = mss_find(dtb)) != OK || (r = qsoc_init_gcc(dtb)) != OK) {
		free(dtb);
		return r;
	}
	free(dtb);

	if ((r = mss_probe()) != OK)
		return r;
	if (stage == STAGE_PROBE) {
		log_info(&mlog, "stage=probe: nothing written\n");
		return OK;
	}
	r = mss_boot(stage == STAGE_MBA);
	if (r != OK) {
		log_warn(&mlog, "the modem did not start (%d)\n", r);
		/* Stay, so that RS does not start us again into a modem in
		 * an unknown state; the log says what happened. */
		return OK;
	}
	up = 1;
	return OK;
}

int
main(int argc, char **argv)
{
	message m;
	int ipc_status;

	env_setargs(argc, argv);
	sef_setcb_init_fresh(mss_init);
	sef_startup();

	if (up)
		(void)sys_setalarm(sys_hz() / 10, 0);
	for (;;) {
		if (sef_receive_status(ANY, &m, &ipc_status) != OK)
			continue;
		if (is_ipc_notify(ipc_status) && m.m_source == CLOCK && up) {
			mss_watch();
			(void)sys_setalarm(sys_hz() / 10, 0);
		}
	}
	return 0;
}
