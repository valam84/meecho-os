/*
 * The USB Type-C port of a Qualcomm SMB2 charger (PM660, PMI8998): who is
 * at the other end of the cable and where VBUS comes from
 * (port/clover/hw-usbhost.md, section 3).
 *
 * Like spmi.c, arithmetic over registers and nothing else: the reads and
 * writes go through struct typec_io, which the driver points at the arbiter
 * and the host test (port/test/qcom_pmic/) at a model of the charger.
 */
#ifndef _QCOM_TYPEC_H
#define _QCOM_TYPEC_H

#include <stddef.h>
#include <stdint.h>

/*
 * The registers, as offsets from the charger's base (its node's reg, 0x1000
 * on the PM660): the charger's peripherals are 0x100 apart above it - OTG
 * at +0x100, USBIN (with the Type-C block in it) at +0x300, DCIN at +0x400,
 * MISC at +0x600, the switcher's clock at +0x900.
 */
#define TC_OTG_RID_CC		0x108	/* bit5 micro-USB mode */
#define TC_OTG_STATUS		0x109	/* bit3 soft start done, 2:0 state */
#define TC_OTG_RT_STS		0x110	/* bit0 fail, bit1 overcurrent */
#define TC_CMD_OTG		0x140	/* bit0 OTG_EN */
#define TC_OTG_CL		0x152	/* 2:0 current limit */
#define TC_OTG_CFG		0x153	/* bit1 VBUS by Type-C, bit4 quickstart */
#define TC_OTG_ENG		0x1c0	/* bit0 halt 1/8 mode; secured */
#define TC_STATUS_1		0x30b	/* five status bytes from here */
#define TC_USBIN_RT_STS		0x310	/* bit4 USBIN plug-in, bit7 change */
#define TC_USBIN_CMD_IL		0x340	/* bit0 USBIN suspend */
#define TC_CFG			0x358
#define TC_CFG_2		0x359
#define TC_CFG_3		0x35a
#define TC_TAPER_TIMER		0x364
#define TC_SW_CTRL		0x368	/* 2:0 power role command */
#define TC_DC_SSUPPLY_CFG2	0x4c1	/* 2:0 OTG soft start; secured */
#define TC_POWER_PATH		0x60b	/* bit4 USE_USBIN, bit0 valid input */
#define TC_FREQ_CLK_DIV		0x950	/* switcher clock; secured */

/* TYPE_C_STATUS_1 (we are the sink): the source's Rp. */
#define TC_RP_DEFAULT		0x80
#define TC_RP_1P5A		0x40
#define TC_RP_3A		0x20
/* TYPE_C_STATUS_2 (we are the source), bits 3:0: what the CC pins see. */
#define TC_DFP_MASK		0x0f
#define TC_DFP_RD_OPEN		0x08	/* a sink: a device, an OTG adapter */
#define TC_DFP_RD_RA_VCONN	0x04	/* a sink through a powered cable */
#define TC_DFP_RD_RD		0x02	/* a debug accessory */
#define TC_DFP_RA_RA		0x01	/* an audio adapter */
/* TYPE_C_STATUS_4. */
#define TC_ST4_DFP		0x80	/* we are the source (downstream) */
#define TC_ST4_VBUS		0x40
#define TC_ST4_DEBOUNCED	0x10
#define TC_ST4_ATTACHED		0x01
/* USBIN's INT_RT_STS. */
#define TC_USBIN_PLUGIN		0x10
/* TYPE_C_INTRPT_ENB_SOFTWARE_CTRL, bits 2:0. */
#define TC_ROLE_MASK		0x07
#define TC_ROLE_DRP		0x00
#define TC_ROLE_UFP		0x04
#define TC_ROLE_DFP		0x02
#define TC_ROLE_OFF		0x01

struct typec_io {
	/* n bytes (1..8) at the charger's base + off; 0 on success. */
	int (*rd)(void *ctx, unsigned off, uint8_t *buf, unsigned n);
	int (*wr)(void *ctx, unsigned off, uint8_t v);
	/* Whether off's peripheral is ours to write. */
	int (*writable)(void *ctx, unsigned off);
	void (*delay_us)(void *ctx, unsigned us);
	void *ctx;
	unsigned base;			/* the charger's, for messages only */
};

/* The charger in the device tree. */
struct typec_chg {
	unsigned sid;			/* its PMIC's USID */
	unsigned base;			/* its reg */
	int pm660;			/* qcom,pm660-charger: the OTG work-around */
	char compat[32];
};

/* OK, with *c filled; ENXIO when the tree has no enabled SMB2 charger. */
int typec_find(const void *dtb, struct typec_chg *c);

/* What is at the other end of the cable. */
enum typec_partner {
	TC_NONE,		/* nothing attached */
	TC_SOURCE,		/* Rp: a host or a charger - we are the device */
	TC_SINK,		/* Rd: a device or an OTG adapter - we are host */
	TC_AUDIO,		/* Ra/Ra */
	TC_DEBUG,		/* Rd/Rd */
	TC_UNKNOWN		/* attached, and the status says nothing known */
};

/* Where VBUS comes from. */
enum typec_vbus {
	TV_NONE,
	TV_EXTERNAL,		/* from the other end: a host or a charger */
	TV_BOOST		/* from our own boost (OTG) */
};

struct typec_state {
	uint8_t st[5];			/* TYPE_C_STATUS_1..5 */
	uint8_t usbin_rt;
	uint8_t otg_status;
	uint8_t cmd_otg;
};

/* Read the state; 0 on success. */
int typec_read(const struct typec_io *io, struct typec_state *s);

enum typec_partner typec_partner(const struct typec_state *s);
enum typec_vbus typec_vbus(const struct typec_state *s);
/* The current the source's Rp offers, in mA (500, 1500, 3000), or 0. */
unsigned typec_rp_ma(const struct typec_state *s);

const char *typec_partner_name(enum typec_partner p);
const char *typec_vbus_name(enum typec_vbus v);

/*
 * One line on the state ("sink (Rd), we are the source; vbus none"), and one
 * on what the registers that govern a role and VBUS hold now - the power
 * role command, VBUS by software or by Type-C, the boost's limit and state,
 * and whose peripherals they are.  Return what snprintf() returns.
 */
int typec_describe(const struct typec_state *s, char *buf, size_t size);
int typec_config_text(const struct typec_io *io, char *buf, size_t size);

/*
 * Every register named above, as "name=0xNN" pairs, for the record a run
 * on the machine leaves: the first is what the boot loader left.
 */
int typec_dump(const struct typec_io *io, char *buf, size_t size);

#endif /* _QCOM_TYPEC_H */
