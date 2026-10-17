/*
 * usb_hub stand: the one structure the driver takes from <minix/usb.h>,
 * and the MINIX integer types it uses beside it.
 */
#ifndef _STUB_MINIX_USB_H
#define _STUB_MINIX_USB_H

#include <stdint.h>

typedef uint8_t u8_t;
typedef uint16_t u16_t;

struct usb_ctrlrequest {
	u8_t bRequestType;
	u8_t bRequest;
	u16_t wValue;
	u16_t wIndex;
	u16_t wLength;
} __attribute__((packed));

#endif
