/*
 * Requests to the qcom_rpm service (minix/drivers/power/qcom_rpm): one key
 * of one RPM resource, for the active set - what Linux's rpmpd.c sends for
 * a power domain's "swen" and "vlvl" (qcom_rpm_smd_write()).  The adreno
 * service votes VDDMX this way for the GPU's clock (port/clover/hw-gpu.md,
 * §20).  qcom_rpm takes the rails' level and enable keys, the IPA's
 * clock by its rate (qcom_ipa; port/clover/hw-modem.md, section 9), and
 * the Wi-Fi's L5 and reference clock (wcn).
 *
 *	request:  m_type QRPM_SET, m_u32.data[QRPM_TYPE .. QRPM_VALUE]
 *	reply:    m_type OK, or an error (EINVAL the RPM said no, ENXIO no
 *		  such resource, EPERM a key or resource not taken, ENODEV
 *		  no RPM link)
 */
#ifndef _MINIX_QCOM_RPM_H
#define _MINIX_QCOM_RPM_H

#define QRPM_SET		0x5100

#define QRPM_TYPE		0	/* m_u32.data[]: the resource type */
#define QRPM_ID			1	/* its number */
#define QRPM_KEY		2	/* the key */
#define QRPM_VALUE		3	/* its value */

/* Resources and keys (include/linux/soc/qcom/smd-rpm.h, rpmpd.c). */
#define QRPM_RES_RWCX		0x78637772	/* "rwcx": VDDCX, by level */
#define QRPM_RES_RWMX		0x786d7772	/* "rwmx": VDDMX, by level */
#define QRPM_KEY_ENABLE		0x6e657773	/* "swen" */
#define QRPM_KEY_LEVEL		0x6c766c76	/* "vlvl" */

/*
 * The WCN3990's supplies (port/clover/hw-wifi.md, section 6): PM660's L5
 * (vdd-0.8-cx-mx, the only one of its four LDOs off at boot) by enable
 * and microvolts (qcom_smd-regulator.c), and its reference clock rf_clk1
 * - an XO buffer controlled by pin (clk-smd-rpm.c), id 4.
 */
#define QRPM_RES_LDOA		0x616f646c	/* "ldoa": PM660's LDOs */
#define QRPM_KEY_UV		0x00007675	/* "uv" */
#define QRPM_RES_CLKA		0x616b6c63	/* "clka": XO buffers */
#define QRPM_KEY_PIN_ENABLE	0x62636370	/* "pccb" */

/* Clocks (clk-smd-rpm.c): the rate in kHz, 0 for off. */
#define QRPM_RES_IPA_CLK	0x00617069	/* "ipa": the IPA's core clock */
#define QRPM_KEY_KHZ		0x007a484b	/* "KHz" */

#endif /* _MINIX_QCOM_RPM_H */
