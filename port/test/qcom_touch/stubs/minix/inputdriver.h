/* Host stand: <minix/inputdriver.h>, recorded rather than sent. */
#ifndef _STUB_MINIX_INPUTDRIVER_H
#define _STUB_MINIX_INPUTDRIVER_H
#include <stdint.h>
#include <time.h>
/* The pages, codes and values are the real header's (-idirafter). */
#include <minix/input.h>
#define FALSE 0
#define TRUE 1
#define INPUT_DEV_KBD	0x01
#define INPUT_DEV_MOUSE	0x02
struct inputdriver {
	void (*idr_leds)(unsigned int);
	void (*idr_intr)(unsigned int);
	void (*idr_alarm)(clock_t);
	void (*idr_other)(message *, int);
};
void inputdriver_send_event(int mouse, unsigned short page,
	unsigned short code, int value, int flags);
void inputdriver_announce(unsigned int type);
void inputdriver_terminate(void);
void inputdriver_task(struct inputdriver *tab);
#endif
