#ifndef _SHIM_MINIX_LOG_H
#define _SHIM_MINIX_LOG_H

/*
 * The driver's logging, reduced to something the stand can count.
 *
 * Counting matters more than printing: several of the checks below assert
 * that the driver complained, and several others assert that it did not.
 * A warning nobody looks at is how the board run that started all this
 * managed to print "the completion names the command at ..." for two
 * hundred lines while the milestone was declared passed.
 */

struct log {
	const char *name;
	int level;
	void (*log_func)(const char *);
};

#define LEVEL_NONE	0
#define LEVEL_WARN	2
#define LEVEL_INFO	3
#define LEVEL_DEBUG	4

void shim_log(struct log *l, int level, const char *fmt, ...);

unsigned shim_warnings(void);
void shim_warnings_reset(void);
const char *shim_last_warning(void);
void shim_verbose(int on);

#define log_warn(l, ...)	shim_log(l, LEVEL_WARN, __VA_ARGS__)
#define log_info(l, ...)	shim_log(l, LEVEL_INFO, __VA_ARGS__)
#define log_debug(l, ...)	shim_log(l, LEVEL_DEBUG, __VA_ARGS__)
#define log_trace(l, ...)	shim_log(l, LEVEL_DEBUG, __VA_ARGS__)

#endif /* _SHIM_MINIX_LOG_H */
