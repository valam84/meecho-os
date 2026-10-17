/*
 * usb_ecm stand: <minix/usb.h>.  The request block has the system's fields
 * by name (the driver reaches them by name and nothing else); the stand's
 * model of xhci answers the protocol (ecmtest.c).  USB_URB_WAITS is the
 * system's value.
 */
#ifndef _STUB_MINIX_USB_H
#define _STUB_MINIX_USB_H

#include <minix/drivers.h>

#define USB_URBSIZE(data_size, iso_count) \
	((data_size) + sizeof(struct usb_urb))

#define USB_TRANSFER_ISO 0
#define USB_TRANSFER_INT 1
#define USB_TRANSFER_CTL 2
#define USB_TRANSFER_BLK 3

#define USB_URB_WAITS	0x40000000UL

#define USB_BASE		0x1100
#define USB_RQ_INIT		(USB_BASE + 0)
#define USB_RQ_DEINIT		(USB_BASE + 1)
#define USB_RQ_SEND_URB		(USB_BASE + 2)
#define USB_REPLY		(USB_BASE + 5)
#define USB_COMPLETE_URB	(USB_BASE + 6)
#define USB_ANNOUCE_DEV		(USB_BASE + 7)
#define USB_WITHDRAW_DEV	(USB_BASE + 8)

struct usb_urb {
	struct usb_urb *next;
	int dev_id;
	int type;
	int endpoint;
	int direction;
	int status;
	int error_count;
	size_t size;
	size_t actual_length;
	void *priv;
	int interval;
	unsigned long transfer_flags;
	unsigned urb_id;
	size_t urb_size;
	int gid;
	size_t iso_desc_offset;
	int number_of_packets;
	int start_frame;
	char setup_packet[8];
	char buffer[1];
};

struct usb_ctrlrequest {
	uint8_t bRequestType;
	uint8_t bRequest;
	uint16_t wValue;
	uint16_t wIndex;
	uint16_t wLength;
} __attribute__((packed));

struct usb_driver {
	void (*urb_completion)(struct usb_urb *urb);
	void (*connect_device)(unsigned dev_id, unsigned int interfaces);
	void (*disconnect_device)(unsigned dev_id);
};

int usb_send_urb(struct usb_urb *urb);
int usb_init(char *name);
int usb_handle_msg(struct usb_driver *ubd, message *msg);

#endif
