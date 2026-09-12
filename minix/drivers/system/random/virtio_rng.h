/*
virtio_rng.h

virtio-rng как аппаратный источник случайного - то, что есть у эмулятора.
Контракт тот же, что у trng.h.
*/

#ifndef VIRTIO_RNG_H
#define VIRTIO_RNG_H

/* OK - устройство есть и первая порция в пуле; ENODEV - машина без него. */
int vrng_init(void);

/* Забрать готовую порцию, если она есть, и заказать следующую. */
size_t vrng_feed(void);

#endif /* VIRTIO_RNG_H */
