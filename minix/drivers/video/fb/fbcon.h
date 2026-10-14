#ifndef __FBCON_H__
#define __FBCON_H__

/* fbcon.c: the console's text on the screen (see the comment there). */
int fbcon_attach(int minor, int kmess);
void fbcon_pull(void);
void fbcon_kmess(void);
void fbcon_tick(void);
void fbcon_stats(char *buf, size_t len);
void fbcon_pause(int paused);
void fbcon_resize(void);

#endif /* __FBCON_H__ */
