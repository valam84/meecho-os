/*
 * usb_hub stand: what the driver takes from <minix/sef.h>, with the stand's
 * own functions behind it (hubtest.c).
 */
#ifndef _STUB_MINIX_SEF_H
#define _STUB_MINIX_SEF_H

#define SEF_INIT_FRESH		0
#define SEF_INIT_LU		1
#define SEF_INIT_RESTART	2

typedef struct { int unused; } sef_init_info_t;

void sef_setcb_init_fresh(int (*cb)(int, sef_init_info_t *));
void sef_setcb_init_lu(int (*cb)(int, sef_init_info_t *));
void sef_setcb_init_restart(int (*cb)(int, sef_init_info_t *));
void sef_setcb_signal_handler(void (*cb)(int));

#endif
