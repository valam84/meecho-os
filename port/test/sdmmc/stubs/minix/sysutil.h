#ifndef _STUB_MINIX_SYSUTIL_H
#define _STUB_MINIX_SYSUTIL_H
#define EP_SET 1
void micro_delay(unsigned long micros);
void default_log(void);
int env_parse(const char *name, const char *fmt, int field, long *param,
	long min, long max);
void env_setargs(int argc, char **argv);
typedef struct { int type; } sef_init_info_t;
void sef_setcb_init_fresh(int (*cb)(int, sef_init_info_t *));
void sef_setcb_signal_handler(void (*cb)(int));
void sef_startup(void);
#endif
