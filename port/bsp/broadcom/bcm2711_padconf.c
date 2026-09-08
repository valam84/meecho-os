/* Implements sys_padconf() for the BCM2711. */

#include "kernel/kernel.h"
#include "arch_proto.h"
#include <sys/types.h>
#include <machine/cpu.h>
#include <minix/mmio.h>
#include <minix/padconf.h>
#include <minix/board.h>
#include <minix/com.h>
#include <assert.h>
#include <io.h>
#include <stdlib.h>
#include <stdio.h>

#include "bsp_padconf.h"
#include "bcm2711_registers.h"

/*
 * On the OMAP parts "padconf" is a dedicated block of pin multiplexing
 * registers. The BCM2711 has no such block: pin function, pull direction and
 * level all live in the GPIO register file. So the GPIO block is what we
 * expose here, and the padconf argument is a register offset within it.
 *
 * Callers therefore drive GPFSEL/GPSET/GPCLR and the pull control registers
 * through sys_padconf(), with the mask restricting the write to the bits they
 * own. That matters: the three function-select bits for one pin sit in the
 * same word as those for nine other pins, and a blind write would reconfigure
 * unrelated hardware.
 */

struct bcm2711_padconf
{
	vir_bytes base;
	vir_bytes size;
};

static struct bcm2711_padconf bcm2711_padconf = {
	.base = 0,
};

static kern_phys_map padconf_phys_map;

int
bsp_padconf_set(u32_t padconf, u32_t mask, u32_t value)
{
	/* Reject offsets outside the GPIO register block. */
	if (padconf >= bcm2711_padconf.size) {
		return EINVAL;
	}

	/* Registers are 32 bits wide and must be accessed aligned. */
	if (padconf & 0x3) {
		return EINVAL;
	}

	set32(bcm2711_padconf.base + padconf, mask, value);

	return OK;
}

void
bsp_padconf_init(void)
{
	bcm2711_padconf.base = BCM2711_GPIO_BASE;
	bcm2711_padconf.size = BCM2711_GPIO_SIZE;

	kern_phys_map_ptr(bcm2711_padconf.base, bcm2711_padconf.size,
	    VMMF_UNCACHED | VMMF_WRITE, &padconf_phys_map,
	    (vir_bytes) & bcm2711_padconf.base);

	return;
}
