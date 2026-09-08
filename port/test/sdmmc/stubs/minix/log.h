/* Стенд: журнал MINIX на printf. */
#ifndef _STUB_MINIX_LOG_H
#define _STUB_MINIX_LOG_H
#include <stdio.h>
#define LEVEL_NONE 0
#define LEVEL_WARN 1
#define LEVEL_INFO 2
#define LEVEL_DEBUG 3
#define LEVEL_TRACE 4
struct log { const char *name; int log_level; void (*log_func)(void); };
extern int stub_log_level;
#define log_at(lvl, tag, l, ...) do { \
	if (stub_log_level >= (lvl)) { \
		printf("%s(%s)", (l)->name, tag); printf(__VA_ARGS__); \
	} \
} while (0)
#define log_warn(l, ...)  log_at(LEVEL_WARN,  "warn",  l, __VA_ARGS__)
#define log_info(l, ...)  log_at(LEVEL_INFO,  "info",  l, __VA_ARGS__)
#define log_debug(l, ...) log_at(LEVEL_DEBUG, "debug", l, __VA_ARGS__)
#define log_trace(l, ...) log_at(LEVEL_TRACE, "trace", l, __VA_ARGS__)
#endif
