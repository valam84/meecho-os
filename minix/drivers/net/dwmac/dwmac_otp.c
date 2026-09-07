/*
 * The one-time-programmable memory of the SoC, read for one thing only:
 * the identifier the station address is made from.
 *
 * This is a whole separate block of the chip - its own registers, its own
 * clocks, its own place in the device tree - and a network driver has no
 * business in it except for this.  It lives here rather than in a driver of
 * its own because there is exactly one consumer, and a service that exists
 * to answer one question at one moment of one other service's start-up
 * would cost more than it explains.  When something else needs the OTP -
 * the thermal trim, the leakage figures, the chip's serial number - this
 * file is what moves out, and the shape it has now is the shape it would
 * take: find the block, let its clocks run, read a range of bytes.
 *
 * The sequence is the vendor driver's (rockchip-otp.c, rk3568_otp_read):
 * enable ECC through the SBPI side channel, put the controller in user
 * mode, then for each two-byte word take the lock, name the address, start
 * the state machine, wait, check the ECC flags, take the word, drop the
 * lock.  Nothing here is derived from a manual - Rockchip publishes none -
 * and nothing is guessed; where the reason for a write is unknown, the
 * comment says so rather than inventing one.
 */

#include <minix/drivers.h>
#include <sys/mman.h>

#include <string.h>

#include "dwmac.h"
#include "dwmacreg.h"
#include "dwmac_socid.h"

/*
 * Words come out two bytes at a time, and this driver asks for one cell of
 * sixteen.  A fixed ceiling rather than an allocation: the request is a
 * constant of this driver, and a bound that cannot be exceeded is easier to
 * be sure of than a buffer that is the right size.
 */
#define DWMAC_OTP_MAX_BYTES	32

static int
otp_wait(uint32_t flag)
{
	unsigned waited;

	for (waited = 0; waited < RK3568_OTP_TIMEOUT_US; waited++) {
		if (dwmac_rd(dwmac.otp, RK3568_OTP_INT_STATUS) & flag) {
			/* The status bits are cleared by writing them back. */
			dwmac_wr(dwmac.otp, RK3568_OTP_INT_STATUS, flag);
			return OK;
		}
		micro_delay(1);
	}

	return EIO;
}

/*
 * Error correction on or off, over the side channel the controller calls
 * SBPI: a small register-write protocol aimed at the OTP macro itself
 * rather than at the controller wrapping it.  Reading with correction off
 * gives the raw cells; the vendor driver turns it on for this read, so this
 * one does too.
 */
static int
otp_ecc_enable(int enable)
{
	dwmac_wr(dwmac.otp, RK3568_OTP_SBPI_CTRL,
	    RK3568_OTP_SBPI_DAP_ADDR_MASK |
	    (RK3568_OTP_SBPI_DAP_ADDR << RK3568_OTP_SBPI_DAP_ADDR_SHIFT));
	dwmac_wr(dwmac.otp, RK3568_OTP_SBPI_CMD_VALID_PRE,
	    RK3568_OTP_SBPI_CMD_VALID_MASK | 0x1);
	dwmac_wr(dwmac.otp, RK3568_OTP_SBPI_CMD0,
	    RK3568_OTP_SBPI_DAP_CMD_WRF | RK3568_OTP_SBPI_DAP_REG_ECC);
	dwmac_wr(dwmac.otp, RK3568_OTP_SBPI_CMD1,
	    enable ? RK3568_OTP_SBPI_ECC_ON : RK3568_OTP_SBPI_ECC_OFF);
	dwmac_wr(dwmac.otp, RK3568_OTP_SBPI_CTRL,
	    RK3568_OTP_SBPI_ENABLE_MASK | RK3568_OTP_SBPI_ENABLE);

	return otp_wait(RK3568_OTP_SBPI_DONE);
}

/*
 * The clocks, switched on for the read and put back the way they were
 * afterwards.  Put back rather than switched off: whether they were running
 * when this driver started is the loader's business, and a driver that
 * turns off a clock it did not turn on is guessing about somebody else.
 */
static uint32_t saved_gate26, saved_gate34;

static void
otp_clocks_on(void)
{
	saved_gate26 = dwmac_rd(dwmac.cru, RK3568_CRU_OTP_CLKGATE);
	saved_gate34 = dwmac_rd(dwmac.cru, RK3568_CRU_OTPPHY_CLKGATE);

	/* A set bit gates the clock off, so switching on writes zeroes. */
	dwmac_wr(dwmac.cru, RK3568_CRU_OTP_CLKGATE,
	    (uint32_t)RK3568_OTP_GATES << 16);
	dwmac_wr(dwmac.cru, RK3568_CRU_OTPPHY_CLKGATE,
	    (uint32_t)RK3568_OTPPHY_GATE << 16);
}

static void
otp_clocks_restore(void)
{
	dwmac_wr(dwmac.cru, RK3568_CRU_OTP_CLKGATE,
	    ((uint32_t)RK3568_OTP_GATES << 16) |
	    (saved_gate26 & RK3568_OTP_GATES));
	dwmac_wr(dwmac.cru, RK3568_CRU_OTPPHY_CLKGATE,
	    ((uint32_t)RK3568_OTPPHY_GATE << 16) |
	    (saved_gate34 & RK3568_OTPPHY_GATE));
}

/*
 * A byte range out of the OTP.  Addresses there count two-byte words, so a
 * request is widened to whole words at both ends and the wanted bytes are
 * taken out of the middle.
 */
static int
otp_read(unsigned offset, uint8_t *out, size_t len)
{
	uint8_t raw[DWMAC_OTP_MAX_BYTES];
	unsigned addr, end, skip, i;
	uint32_t qp, q;
	int r;

	addr = offset / RK3568_OTP_NBYTES;
	end = (offset + (unsigned)len + RK3568_OTP_NBYTES - 1) /
	    RK3568_OTP_NBYTES;
	skip = offset % RK3568_OTP_NBYTES;

	if ((end - addr) * RK3568_OTP_NBYTES > sizeof(raw))
		return EINVAL;

	otp_clocks_on();

	if ((r = otp_ecc_enable(TRUE)) != OK) {
		log_warn(&dwmac_log, "otp: the ecc setup did not finish\n");
		goto out;
	}

	dwmac_wr(dwmac.otp, RK3568_OTP_USER_CTRL,
	    RK3568_OTP_USE_USER | RK3568_OTP_USE_USER_MASK);
	micro_delay(5);

	for (i = 0; addr < end; addr++, i += RK3568_OTP_NBYTES) {
		dwmac_wr(dwmac.otp, RK3568_OTP_LOCK_CTRL,
		    RK3568_OTP_LOCK | RK3568_OTP_LOCK_MASK);
		dwmac_wr(dwmac.otp, RK3568_OTP_USER_ADDR,
		    addr | RK3568_OTP_USER_ADDR_MASK);
		dwmac_wr(dwmac.otp, RK3568_OTP_USER_ENABLE,
		    RK3568_OTP_USER_FSM_ENABLE |
		    RK3568_OTP_USER_FSM_ENABLE_MASK);

		if ((r = otp_wait(RK3568_OTP_USER_DONE)) != OK) {
			log_warn(&dwmac_log, "otp: word %u never arrived\n",
			    addr);
			break;
		}

		/*
		 * What the correction made of the word.  Both halves of this
		 * test come from the vendor driver, which does not say what
		 * the bits are called; what they mean between them is that
		 * the word came back wrong, and a wrong word here would be a
		 * plausible-looking station address made of nothing.
		 */
		qp = dwmac_rd(dwmac.otp, RK3568_OTP_USER_QP);
		if ((qp & 0xc0) == 0xc0 || (qp & 0x20)) {
			log_warn(&dwmac_log, "otp: word %u failed its ecc "
			    "check (qp 0x%x)\n", addr, qp);
			r = EIO;
			break;
		}

		q = dwmac_rd(dwmac.otp, RK3568_OTP_USER_Q);
		raw[i] = (uint8_t)(q & 0xff);
		raw[i + 1] = (uint8_t)((q >> 8) & 0xff);

		dwmac_wr(dwmac.otp, RK3568_OTP_LOCK_CTRL,
		    RK3568_OTP_LOCK_MASK);
	}

	if (r != OK)
		dwmac_wr(dwmac.otp, RK3568_OTP_LOCK_CTRL, RK3568_OTP_LOCK_MASK);
	else
		memcpy(out, raw + skip, len);

	dwmac_wr(dwmac.otp, RK3568_OTP_USER_CTRL, RK3568_OTP_USE_USER_MASK);

out:
	otp_clocks_restore();
	return r;
}

int
dwmac_otp_map(void)
{
	void *v;

	if (dwmac.info.otp_base == 0 || dwmac.info.soc_id_size == 0)
		return ENXIO;

	v = vm_map_phys(SELF, (void *)dwmac.info.otp_base,
	    dwmac.info.otp_size);
	if (v == MAP_FAILED) {
		log_warn(&dwmac_log, "cannot map the OTP at 0x%lx\n",
		    (unsigned long)dwmac.info.otp_base);
		return EPERM;
	}
	dwmac.otp = (vir_bytes)v;

	return OK;
}

/*
 * The station address this board is known by, or a failure that leaves the
 * caller to decide what to do instead.  Nothing here falls back to an
 * invented address: the point of the whole file is that the address is not
 * invented, and a fallback hidden in the middle of it would make "the OTP
 * was not read" indistinguishable from "the OTP said this".
 */
int
dwmac_otp_hwaddr(netdriver_addr_t *addr)
{
	uint8_t id[DWMAC_OTP_MAX_BYTES];
	uint8_t mac[6];
	size_t len = dwmac.info.soc_id_size;
	int r;

	if (dwmac.otp == 0)
		return ENXIO;
	if (len < DWMAC_SOC_ID_MIN || len > sizeof(id))
		return EINVAL;

	if ((r = otp_read(dwmac.info.soc_id_offset, id, len)) != OK)
		return r;

	if (dwmac_hwaddr_from_soc_id(id, len, mac) != 0) {
		log_warn(&dwmac_log, "otp: the soc-id cell holds nothing "
		    "usable\n");
		return EINVAL;
	}

	memcpy(addr->na_addr, mac, sizeof(mac));

	return OK;
}
