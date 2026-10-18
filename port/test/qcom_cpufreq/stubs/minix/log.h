/* Host stand: the MINIX log, captured line by line for the checks
 * (model.c, stub_log()) and printed with -v. */
#ifndef _STUB_MINIX_LOG_H
#define _STUB_MINIX_LOG_H
#define LEVEL_NONE 0
#define LEVEL_WARN 1
#define LEVEL_INFO 2
#define LEVEL_DEBUG 3
#define LEVEL_TRACE 4
struct log { const char *name; int log_level; void (*log_func)(void); };
void stub_log(int level, const char *tag, const struct log *l,
	const char *fmt, ...) __attribute__((format(printf, 4, 5)));
#define log_warn(l, ...)  stub_log(LEVEL_WARN,  "warn",  l, __VA_ARGS__)
#define log_info(l, ...)  stub_log(LEVEL_INFO,  "info",  l, __VA_ARGS__)
#define log_debug(l, ...) stub_log(LEVEL_DEBUG, "debug", l, __VA_ARGS__)
#define log_trace(l, ...) stub_log(LEVEL_TRACE, "trace", l, __VA_ARGS__)
#endif
