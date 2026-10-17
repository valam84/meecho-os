/*
 * The Type-C port of a Qualcomm SMB2 charger (typec.h).
 *
 * The state is read the way the charger driver of the Mi Pad 4's own
 * Android kernel reads it (smb-lib.c, smblib_get_prop_typec_mode()): five
 * status bytes from TYPE_C_STATUS_1, and the "we are the source" bit of the
 * fourth decides which of the first two means anything.  As the source we
 * look at what pulls our CC pins down (Rd: a device; Ra: an audio adapter
 * or a powered cable); as the sink at which current the source's Rp offers.
 * A status that is neither is not guessed at.
 *
 * VBUS: our own boost when the OTG block says it is on (or we asked it to
 * be), otherwise the other end's when USBIN sees a plug-in.  The boost's
 * own VBUS raises the same plug-in bit, which is why the boost is asked
 * first (the Android driver calls such an insertion fake).
 */

#include <minix/fdt.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "typec.h"

/*===========================================================================*
 *    the charger in the device tree                                         *
 *===========================================================================*/
#define TC_MAXDEPTH	16

static const struct {
	const char *compatible;
	int pm660;
} chargers[] = {
	{ "qcom,pm660-charger",		1 },
	{ "qcom,pmi8998-charger",	0 },
};

struct tc_find {
	struct typec_chg *c;
	int sid[TC_MAXDEPTH];		/* a PMIC's USID by depth, or -1 */
	int found, bad;
};

static int
enabled(const struct fdt_node *node)
{
	const char *s;
	unsigned len;

	if ((s = fdt_getprop(node, "status", &len)) == NULL)
		return 1;
	return strcmp(s, "okay") == 0 || strcmp(s, "ok") == 0;
}

static int
first_cell(const struct fdt_node *node, const char *prop, uint32_t *v)
{
	const void *p;
	unsigned len;

	if ((p = fdt_getprop(node, prop, &len)) == NULL || len < 4)
		return -1;
	*v = (uint32_t)fdt_read_cells(p, 1);
	return 0;
}

static int
find_chg(void *cookie, int depth, const char *name, const struct fdt_node *node)
{
	struct tc_find *f = cookie;
	uint32_t v;
	unsigned i;

	(void)name;
	if (depth < 0 || depth >= TC_MAXDEPTH)
		return 0;
	f->sid[depth] = -1;

	/* A PMIC on the SPMI bus: reg is <usid type>. */
	if (fdt_node_is_compatible(node, "qcom,spmi-pmic")) {
		if (first_cell(node, "reg", &v) == 0)
			f->sid[depth] = (int)v;
		return 0;
	}

	for (i = 0; i < sizeof(chargers) / sizeof(chargers[0]); i++)
		if (fdt_node_is_compatible(node, chargers[i].compatible))
			break;
	if (i == sizeof(chargers) / sizeof(chargers[0]) || !enabled(node))
		return 0;
	if (depth == 0 || f->sid[depth - 1] < 0 ||
	    first_cell(node, "reg", &v) != 0 || v == 0 || v > 0xf000) {
		f->bad = 1;
		return 0;
	}
	f->c->sid = (unsigned)f->sid[depth - 1];
	f->c->base = v;
	f->c->pm660 = chargers[i].pm660;
	strlcpy(f->c->compat, chargers[i].compatible, sizeof(f->c->compat));
	f->found = 1;
	return 1;
}

int
typec_find(const void *dtb, struct typec_chg *c)
{
	struct tc_find f;

	memset(&f, 0, sizeof(f));
	memset(c, 0, sizeof(*c));
	f.c = c;
	fdt_walk(dtb, find_chg, &f);
	return f.found && !f.bad ? 0 : ENXIO;
}

/*===========================================================================*
 *    the state                                                              *
 *===========================================================================*/
int
typec_read(const struct typec_io *io, struct typec_state *s)
{
	memset(s, 0, sizeof(*s));
	if (io->rd(io->ctx, TC_STATUS_1, s->st, sizeof(s->st)) != 0 ||
	    io->rd(io->ctx, TC_USBIN_RT_STS, &s->usbin_rt, 1) != 0 ||
	    io->rd(io->ctx, TC_OTG_STATUS, &s->otg_status, 1) != 0 ||
	    io->rd(io->ctx, TC_CMD_OTG, &s->cmd_otg, 1) != 0)
		return -1;
	return 0;
}

enum typec_partner
typec_partner(const struct typec_state *s)
{
	if (s->st[3] & TC_ST4_DFP) {
		switch (s->st[1] & TC_DFP_MASK) {
		case TC_DFP_RD_OPEN:
		case TC_DFP_RD_RA_VCONN:
			return TC_SINK;
		case TC_DFP_RA_RA:
			return TC_AUDIO;
		case TC_DFP_RD_RD:
			return TC_DEBUG;
		}
	} else {
		/* The whole byte, as Android compares it: one Rp, nothing else. */
		switch (s->st[0]) {
		case TC_RP_DEFAULT:
		case TC_RP_1P5A:
		case TC_RP_3A:
			return TC_SOURCE;
		}
	}
	return (s->st[3] & TC_ST4_ATTACHED) ? TC_UNKNOWN : TC_NONE;
}

unsigned
typec_rp_ma(const struct typec_state *s)
{
	if (typec_partner(s) != TC_SOURCE)
		return 0;
	return s->st[0] == TC_RP_3A ? 3000 : s->st[0] == TC_RP_1P5A ? 1500 :
	    500;
}

enum typec_vbus
typec_vbus(const struct typec_state *s)
{
	/* OTG_STATUS bits 2:0 read 2 while the boost runs. */
	if ((s->otg_status & 0x07) == 0x02 || (s->cmd_otg & 0x01))
		return TV_BOOST;
	if (s->usbin_rt & TC_USBIN_PLUGIN)
		return TV_EXTERNAL;
	return TV_NONE;
}

const char *
typec_partner_name(enum typec_partner p)
{
	switch (p) {
	case TC_NONE:		return "none";
	case TC_SOURCE:		return "source";
	case TC_SINK:		return "sink";
	case TC_AUDIO:		return "audio";
	case TC_DEBUG:		return "debug";
	default:		return "unknown";
	}
}

const char *
typec_vbus_name(enum typec_vbus v)
{
	switch (v) {
	case TV_EXTERNAL:	return "external";
	case TV_BOOST:		return "boost";
	default:		return "none";
	}
}

int
typec_describe(const struct typec_state *s, char *buf, size_t size)
{
	enum typec_partner p = typec_partner(s);
	const char *what;

	switch (p) {
	case TC_SOURCE:
		what = "Rp: a host or a charger, we are the device";
		break;
	case TC_SINK:
		what = (s->st[1] & TC_DFP_MASK) == TC_DFP_RD_RA_VCONN ?
		    "Rd and Ra: a device through a powered cable, we are the "
		    "host" : "Rd: a device or an OTG adapter, we are the host";
		break;
	case TC_AUDIO:
		what = "Ra/Ra: an audio adapter";
		break;
	case TC_DEBUG:
		what = "Rd/Rd: a debug accessory";
		break;
	case TC_UNKNOWN:
		what = "attached, nothing this driver knows";
		break;
	default:
		what = "nothing attached";
		break;
	}
	if (p == TC_SOURCE)
		return snprintf(buf, size, "type-c %s (%s, %u mA); vbus %s",
		    typec_partner_name(p), what, typec_rp_ma(s),
		    typec_vbus_name(typec_vbus(s)));
	return snprintf(buf, size, "type-c %s (%s); vbus %s",
	    typec_partner_name(p), what, typec_vbus_name(typec_vbus(s)));
}

/*===========================================================================*
 *    what the boot loader left                                              *
 *===========================================================================*/
static const char *
role_name(uint8_t ctl)
{
	switch (ctl & TC_ROLE_MASK) {
	case TC_ROLE_DRP:	return "dual role";
	case TC_ROLE_UFP:	return "sink only";
	case TC_ROLE_DFP:	return "source only";
	case TC_ROLE_OFF:	return "off";
	default:		return "?";
	}
}

int
typec_config_text(const struct typec_io *io, char *buf, size_t size)
{
	uint8_t ctl, cfg, cl, st, cmd, rid;

	if (io->rd(io->ctx, TC_SW_CTRL, &ctl, 1) != 0 ||
	    io->rd(io->ctx, TC_OTG_CFG, &cfg, 1) != 0 ||
	    io->rd(io->ctx, TC_OTG_CL, &cl, 1) != 0 ||
	    io->rd(io->ctx, TC_OTG_STATUS, &st, 1) != 0 ||
	    io->rd(io->ctx, TC_CMD_OTG, &cmd, 1) != 0 ||
	    io->rd(io->ctx, TC_OTG_RID_CC, &rid, 1) != 0)
		return snprintf(buf, size, "type-c registers unreadable");
	return snprintf(buf, size, "%s port, %s (0x%04x = 0x%02x); VBUS by "
	    "%s (0x%04x = 0x%02x); boost %s (CMD_OTG 0x%02x, status 0x%02x), "
	    "limit %u mA; OTG block %s, USBIN %s",
	    (rid & 0x20) ? "micro-USB" : "Type-C", role_name(ctl),
	    io->base + TC_SW_CTRL, ctl,
	    (cfg & 0x02) ? "Type-C" : "software", io->base + TC_OTG_CFG, cfg,
	    (cmd & 0x01) ? "on" : "off", cmd, st, 250 + 250 * (cl & 0x07),
	    io->writable(io->ctx, TC_CMD_OTG) ? "ours" : "not ours",
	    io->writable(io->ctx, TC_USBIN_CMD_IL) ? "ours" : "not ours");
}

static const unsigned dump_regs[] = {
	TC_OTG_RID_CC, TC_OTG_STATUS, TC_OTG_RT_STS, TC_CMD_OTG, TC_OTG_CL,
	TC_OTG_CFG, TC_OTG_ENG, TC_STATUS_1, TC_STATUS_1 + 1, TC_STATUS_1 + 2,
	TC_STATUS_1 + 3, TC_STATUS_1 + 4, TC_USBIN_RT_STS, TC_USBIN_CMD_IL,
	TC_CFG, TC_CFG_2, TC_CFG_3, TC_TAPER_TIMER, TC_SW_CTRL,
	TC_DC_SSUPPLY_CFG2, TC_POWER_PATH, TC_FREQ_CLK_DIV,
};

int
typec_dump(const struct typec_io *io, char *buf, size_t size)
{
	unsigned i;
	uint8_t v;
	int n = 0;

	buf[0] = '\0';
	for (i = 0; i < sizeof(dump_regs) / sizeof(dump_regs[0]) &&
	    n < (int)size; i++) {
		if (io->rd(io->ctx, dump_regs[i], &v, 1) != 0)
			n += snprintf(buf + n, size - n, "%s%04x:--", n ? " " :
			    "", io->base + dump_regs[i]);
		else
			n += snprintf(buf + n, size - n, "%s%04x:%02x", n ? " " :
			    "", io->base + dump_regs[i], v);
	}
	return n;
}
