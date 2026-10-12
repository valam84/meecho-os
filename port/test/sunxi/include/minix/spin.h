/* Стенд sunxi: spin_t на счётчике архитектурного таймера - настоящие
 * микросекунды, как у libsys, а не обороты. */
#ifndef _H_MINIX_SPIN_H
#define _H_MINIX_SPIN_H
#include <stdint.h>
uint64_t harness_now_us(void);
typedef struct { uint64_t end; } spin_t;
static inline void spin_init(spin_t *s, unsigned long usecs)
{
	s->end = harness_now_us() + usecs;
}
static inline int spin_check(spin_t *s)
{
	return harness_now_us() < s->end;
}
#endif
