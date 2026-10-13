/*
 * Стенд mali: журнал, который стенд может прочесть.  Каждая строка копится
 * в буфер, и проверки спрашивают, сказал ли драйвер то, что должен, и не
 * сказал ли лишнего; предупреждения считаются отдельно.
 */
#ifndef _SHIM_MINIX_LOG_H
#define _SHIM_MINIX_LOG_H
#define LEVEL_NONE	0
#define LEVEL_WARN	1
#define LEVEL_INFO	2
#define LEVEL_DEBUG	3
#define LEVEL_TRACE	4
struct log { const char *name; int log_level; void (*log_func)(void); };
void shim_log(struct log *l, int level, const char *fmt, ...)
	__attribute__((format(printf, 3, 4)));
#define log_warn(l, ...)	shim_log(l, LEVEL_WARN, __VA_ARGS__)
#define log_info(l, ...)	shim_log(l, LEVEL_INFO, __VA_ARGS__)
#define log_debug(l, ...)	shim_log(l, LEVEL_DEBUG, __VA_ARGS__)
#define log_trace(l, ...)	shim_log(l, LEVEL_TRACE, __VA_ARGS__)
#endif
