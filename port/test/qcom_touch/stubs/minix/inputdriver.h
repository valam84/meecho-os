/* Host stand: <minix/inputdriver.h>, recorded rather than sent. */
#ifndef _STUB_MINIX_INPUTDRIVER_H
#define _STUB_MINIX_INPUTDRIVER_H
#include <time.h>
#define FALSE 0
#define INPUT_PAGE_KEY	0x07
#define INPUT_PRESS	1
#define INPUT_RELEASE	0
#define INPUT_DEV_KBD	0x01
struct inputdriver {
	void (*idr_leds)(unsigned int);
	void (*idr_intr)(unsigned int);
	void (*idr_alarm)(clock_t);
};
void inputdriver_send_event(int mouse, unsigned short page,
	unsigned short code, int value, int flags);
void inputdriver_announce(unsigned int type);
void inputdriver_terminate(void);
void inputdriver_task(struct inputdriver *tab);
#endif
