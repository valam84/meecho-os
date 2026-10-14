/*
 * meechowm-notify: the desktop's word that a system service has gone and
 * come back (stage 11.7, "a fall is seen").  RS restarts a driver or a
 * server that died - or that "minix-service refresh" told to - and until
 * now the only trace of it was a line on a console nobody reads.  This
 * puts a card in the top right corner of the screen, under the panel, for
 * a few seconds: "lwip restarted".
 *
 * Where it comes from: /proc/service, RS's own table as procfs shows it -
 * one file a service, with its endpoint and its count of restarts.  RS
 * gives a restarted service a new endpoint and adds one to the count, so
 * what is looked at is a state, not an event: a look that comes late sees
 * the restart all the same, and two restarts between looks are two in the
 * count.  Looked at once a second; why a look and not a subscription to
 * DS is in the PORTING-LOG ("Стойкость: уведомление о перезапуске").
 *
 *   svc.c     the look at /proc/service, and what changed since the last
 *   card.c    a card: its size and its picture (pixman, fcft)
 *   notify.c  the layer surface, the cards on it, and the modes for the
 *             stand and the board (-d, -m, -R)
 */
#ifndef NOTIFY_H
#define NOTIFY_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define NT_LABEL	32	/* RS_MAX_LABEL_LEN is 16; room to spare */

enum nt_kind {
	NT_RESTART,	/* a new endpoint, or the count of restarts grown */
	NT_STOPPED,	/* gone from RS's table, and not back */
	NT_BACK		/* in RS's table again, after it was said gone */
};

struct nt_event {
	enum nt_kind kind;
	char label[NT_LABEL];
	int endpoint;		/* now; the one it had, for NT_STOPPED */
	int pid;
	int restarts;		/* RS's count, now */
	int times;		/* restarts since the last look: at least 1 */
};

/* svc.c */
int nt_look(struct nt_event *ev, int max);
int nt_services(void);

/* card.c */
struct nt_card;
void nt_card_init(int screen_height);
struct nt_card *nt_card_new(const struct nt_event *ev, const char *when);
void nt_card_free(struct nt_card *c);
void nt_card_size(const struct nt_card *c, int *w, int *h);
void nt_card_title_band(const struct nt_card *c, int *x, int *y, int *w,
	int *h);
void nt_card_draw(const struct nt_card *c, uint32_t *px, int stride, int x,
	int y, int buf_w, int buf_h);
void nt_card_text(const struct nt_card *c, FILE *f);

#endif
