/* Host stand: <minix/sysutil.h> as far as the service uses it; time is
 * the model's. */
#ifndef _STUB_MINIX_SYSUTIL_H
#define _STUB_MINIX_SYSUTIL_H
void micro_delay(unsigned long micros);
unsigned sys_hz(void);
/* The free-running clock: here the model's microseconds (osmodel.c). */
void read_frclock_64(u64_t *frclk);
u64_t delta_frclock_64(u64_t base, u64_t cur);
#endif
