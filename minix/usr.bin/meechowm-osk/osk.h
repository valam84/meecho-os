/*
 * meechowm-osk: the desktop's on-screen keyboard (stage 11.9).
 *
 *   osk.c    the client: the layer surface, up while meechowm's input
 *            method is active; touches and the pointer into libosk, its
 *            keys out through a virtual keyboard
 *   draw.c   the picture: libosk's keys, in MEECHO's colours
 */
#ifndef MEECHOWM_OSK_H
#define MEECHOWM_OSK_H

#include <stdint.h>

struct osk_view;

/* draw.c */
void kb_draw(uint32_t *px, int w, int h, int stride,
	const struct osk_view *v);

/* hid2evdev.c (meechowm's) */
uint32_t mc_hid_to_evdev(unsigned usage);

#endif
