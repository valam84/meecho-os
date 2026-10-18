/*
 * The on-screen keyboard's owner: touches in, keys out (<minix/libosk.h>).
 */
#include <stddef.h>
#include <minix/libosk.h>

static const uint8_t mod_usage[OSK_NMODS] = {
	[OSK_SHIFT] = OSK_U_LSHIFT, [OSK_CTRL] = OSK_U_LCTRL,
	[OSK_ALT] = OSK_U_LALT,
};

void
osk_init(struct osk *o, unsigned w, unsigned h,
	void (*emit)(void *arg, unsigned usage, int down), void *arg)
{
	unsigned i;

	o->w = w;
	o->h = h;
	o->emit = emit;
	o->arg = arg;
	o->base = OSK_LOWER;
	for (i = 0; i < OSK_NMODS; i++)
		o->mod[i] = OSK_OFF;
	for (i = 0; i < OSK_MAX_TOUCH; i++) {
		o->t[i].active = 0;
		o->t[i].key = NULL;
	}
	o->seq = 0;
}

void
osk_resize(struct osk *o, unsigned w, unsigned h)
{
	o->w = w;
	o->h = h;
}

unsigned
osk_shown(const struct osk *o)
{
	if (o->base == OSK_LOWER && o->mod[OSK_SHIFT] != OSK_OFF)
		return OSK_UPPER;
	return o->base;
}

void
osk_rect(const struct osk_key *k, unsigned w, unsigned h, unsigned *x0,
	unsigned *y0, unsigned *x1, unsigned *y1)
{
	*x0 = (unsigned)k->x * w / OSK_UNITS;
	*x1 = (unsigned)(k->x + k->w) * w / OSK_UNITS;
	*y0 = (unsigned)k->row * h / OSK_ROWS;
	*y1 = (unsigned)(k->row + 1) * h / OSK_ROWS;
}

const struct osk_key *
osk_hit(unsigned layer, unsigned x, unsigned y, unsigned w, unsigned h)
{
	const struct osk_layer *l;
	unsigned i, x0, y0, x1, y1;

	if (layer >= OSK_NLAYERS || x >= w || y >= h)
		return NULL;
	l = &osk_layers[layer];
	for (i = 0; i < l->nkeys; i++) {
		osk_rect(&l->keys[i], w, h, &x0, &y0, &x1, &y1);
		if (x >= x0 && x < x1 && y >= y0 && y < y1)
			return &l->keys[i];
	}
	return NULL;
}

unsigned
osk_console_height(unsigned h)
{
	return h * OSK_CONSOLE_PERMILLE / 1000 / OSK_ROWS * OSK_ROWS;
}

/* A modifier or layer key: it acts now, and sends nothing. */
static void
act(struct osk *o, const struct osk_key *k)
{
	uint8_t *m;

	if (k->act == OSK_A_MOD && k->usage < OSK_NMODS) {
		m = &o->mod[k->usage];
		*m = *m == OSK_OFF ? OSK_ARMED : *m == OSK_ARMED ? OSK_LOCKED :
		    OSK_OFF;
	} else if (k->act == OSK_A_LAYER && k->usage < OSK_NLAYERS &&
	    k->usage != OSK_UPPER) {
		o->base = k->usage;
		/* An armed Shift was for a letter of the layer left. */
		if (o->mod[OSK_SHIFT] == OSK_ARMED)
			o->mod[OSK_SHIFT] = OSK_OFF;
	}
}

static void
press(struct osk *o, unsigned id, unsigned x, unsigned y)
{
	const struct osk_key *k;
	unsigned shown = osk_shown(o), i;

	k = osk_hit(shown, x, y, o->w, o->h);
	o->t[id].active = 1;
	o->t[id].key = k;
	o->t[id].seq = ++o->seq;
	o->t[id].shift = o->t[id].ctrl = o->t[id].alt = 0;
	if (k == NULL)
		return;
	if (k->act != OSK_A_KEY) {
		act(o, k);
		return;
	}

	if (o->mod[OSK_CTRL] != OSK_OFF) {
		o->emit(o->arg, OSK_U_LCTRL, 1);
		o->t[id].ctrl = 1;
	}
	if (o->mod[OSK_ALT] != OSK_OFF) {
		o->emit(o->arg, OSK_U_LALT, 1);
		o->t[id].alt = 1;
	}
	if ((k->flags & OSK_K_SHIFT) ||
	    (shown == OSK_UPPER && !(k->flags & OSK_K_PLAIN))) {
		o->emit(o->arg, OSK_U_LSHIFT, 1);
		o->t[id].shift = 1;
	}
	o->emit(o->arg, k->usage, 1);

	/* An armed modifier was for this key. */
	for (i = 0; i < OSK_NMODS; i++)
		if (o->mod[i] == OSK_ARMED)
			o->mod[i] = OSK_OFF;
}

static void
release(struct osk *o, unsigned id)
{
	const struct osk_key *k = o->t[id].key;

	if (!o->t[id].active)
		return;
	if (k != NULL && k->act == OSK_A_KEY) {
		/* What went down with it comes up after it, the other way. */
		o->emit(o->arg, k->usage, 0);
		if (o->t[id].shift)
			o->emit(o->arg, mod_usage[OSK_SHIFT], 0);
		if (o->t[id].alt)
			o->emit(o->arg, mod_usage[OSK_ALT], 0);
		if (o->t[id].ctrl)
			o->emit(o->arg, mod_usage[OSK_CTRL], 0);
	}
	o->t[id].active = 0;
	o->t[id].key = NULL;
	o->t[id].shift = o->t[id].ctrl = o->t[id].alt = 0;
}

int
osk_touch(struct osk *o, unsigned id, int event, unsigned x, unsigned y)
{
	uint32_t before = osk_word(o);

	if (id >= OSK_MAX_TOUCH)
		return 0;
	switch (event) {
	case OSK_TOUCH_DOWN:
		if (o->t[id].active)
			release(o, id);	/* a down we missed the up of */
		press(o, id, x, y);
		break;
	case OSK_TOUCH_CONTACT:
		if (!o->t[id].active)
			press(o, id, x, y);	/* the down was missed */
		break;
	case OSK_TOUCH_UP:
		release(o, id);
		break;
	}
	return osk_word(o) != before;
}

int
osk_lost(struct osk *o, unsigned id)
{
	uint32_t before = osk_word(o);

	if (id < OSK_MAX_TOUCH)
		release(o, id);
	return osk_word(o) != before;
}

int
osk_reset(struct osk *o)
{
	uint32_t before = osk_word(o);
	unsigned i;

	for (i = 0; i < OSK_MAX_TOUCH; i++)
		release(o, i);
	for (i = 0; i < OSK_NMODS; i++)
		o->mod[i] = OSK_OFF;
	return osk_word(o) != before;
}

/*
 * The key of layer where k is: a key held down from another layer (Shift,
 * touched on OSK_LOWER, is drawn on OSK_UPPER) shows by its place.
 */
static int
key_in(unsigned layer, const struct osk_key *k)
{
	const struct osk_layer *l = &osk_layers[layer];
	unsigned i;

	for (i = 0; i < l->nkeys; i++)
		if (l->keys[i].row == k->row && l->keys[i].x == k->x)
			return (int)i;
	return -1;
}

void
osk_view(const struct osk *o, struct osk_view *v)
{
	unsigned i, best = 0;

	v->layer = osk_shown(o);
	for (i = 0; i < OSK_NMODS; i++)
		v->mod[i] = o->mod[i];
	v->pressed = -1;
	/* The key held down last, of those still held. */
	for (i = 0; i < OSK_MAX_TOUCH; i++)
		if (o->t[i].active && o->t[i].key != NULL &&
		    o->t[i].seq > best) {
			best = o->t[i].seq;
			v->pressed = key_in(v->layer, o->t[i].key);
		}
}

/*
 * The word: bits 0-2 the layer shown, 4-5 Shift, 6-7 Ctrl, 8-9 Alt, 16-23
 * the pressed key plus one (0: none).
 */
uint32_t
osk_word(const struct osk *o)
{
	struct osk_view v;

	osk_view(o, &v);
	return (uint32_t)v.layer | (uint32_t)v.mod[OSK_SHIFT] << 4 |
	    (uint32_t)v.mod[OSK_CTRL] << 6 | (uint32_t)v.mod[OSK_ALT] << 8 |
	    (uint32_t)(v.pressed + 1) << 16;
}

void
osk_unword(uint32_t word, struct osk_view *v)
{
	unsigned layer = word & 7, pressed = (word >> 16) & 0xff;

	v->layer = layer < OSK_NLAYERS ? layer : OSK_LOWER;
	v->mod[OSK_SHIFT] = (word >> 4) & 3;
	v->mod[OSK_CTRL] = (word >> 6) & 3;
	v->mod[OSK_ALT] = (word >> 8) & 3;
	v->pressed = pressed != 0 && pressed <= osk_layers[v->layer].nkeys ?
	    (int)pressed - 1 : -1;
}
