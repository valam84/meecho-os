/* Стенд: spin_t как счётчик оборотов. Модель контроллера отвечает сразу,
 * поэтому таймаут срабатывает только там, где его и проверяют. */
#ifndef _STUB_MINIX_SPIN_H
#define _STUB_MINIX_SPIN_H
typedef struct { long left; } spin_t;
static inline void spin_init(spin_t *s, unsigned long usecs)
{
	s->left = (long)(usecs / 10) + 2;
}
static inline int spin_check(spin_t *s)
{
	return (--s->left > 0);
}
#endif
