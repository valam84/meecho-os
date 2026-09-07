/*
 * The station address this board's own identifier gives.
 *
 * Kept apart from everything else, and free of any header of this system,
 * so that a host compiler can build it against the vector taken off the
 * running board - see port/test/dwmac/.  Same arrangement, and the same
 * reason, as dwmac_pins.c: a pure computation whose mistakes are silent.
 */
#ifndef _DWMAC_SOCID_H
#define _DWMAC_SOCID_H

#include <stddef.h>
#include <stdint.h>

#define DWMAC_SOC_ID_MIN	8	/* what the vendor driver demands */

int dwmac_hwaddr_from_soc_id(const uint8_t *id, size_t len, uint8_t *addr);

#endif /* _DWMAC_SOCID_H */
