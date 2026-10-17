/*
 * usb_ecm stand: <minix/log.h>, with the lines counted by level, so that
 * a check can ask whether the driver warned - and about what.
 */
#ifndef _STUB_MINIX_LOG_H
#define _STUB_MINIX_LOG_H

#define LEVEL_NONE	0
#define LEVEL_WARN	1
#define LEVEL_INFO	2
#define LEVEL_DEBUG	3
#define LEVEL_TRACE	4

struct log {
	const char *name;
	int log_level;
	void (*log_func)(struct log *log, int level, const char *fmt, ...);
};

void default_log(struct log *log, int level, const char *fmt, ...)
	__attribute__((format(printf, 3, 4)));

#define log_warn(l, ...)	default_log((l), LEVEL_WARN, __VA_ARGS__)
#define log_info(l, ...)	default_log((l), LEVEL_INFO, __VA_ARGS__)
#define log_debug(l, ...)	default_log((l), LEVEL_DEBUG, __VA_ARGS__)
#define log_trace(l, ...)	default_log((l), LEVEL_TRACE, __VA_ARGS__)

#endif
