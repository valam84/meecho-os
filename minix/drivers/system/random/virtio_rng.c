/*
 * Аппаратный источник случайного на эмуляторе: virtio-rng.
 *
 * Та же роль, что у trng.c на плате, и тот же контракт: найти устройство,
 * если оно у машины есть, и подсыпать из него в пул. На CB2 источник -
 * TRNG внутри SoC; у QEMU virt такого блока нет, зато есть virtio-rng,
 * которое отдаёт гостю случайные байты хоста. Без него пул на эмуляторе не
 * сеется вовсе: времена прерываний простаивающей виртуальной машины дают
 * единицы отсчётов в минуту, /dev/random отвечает EAGAIN, и всё, что
 * хочет зерна до начала работы - libcrypto первым, - отказывает. То есть
 * TLS нельзя было бы проверить на эмуляторе в принципе, только на плате.
 *
 * Устройство простейшее из семейства: одна очередь, в неё кладётся буфер
 * на запись, устройство заполняет его сколько сможет и отдаёт обратно с
 * длиной. Прерывание не нужно: служба и так просыпается по своему таймеру
 * и спрашивает, не вернулся ли буфер. Одно ожидание есть при старте -
 * первая порция должна лечь в пул до того, как кто-нибудь его спросит,
 * ровно как у TRNG.
 *
 * Код не привязан к архитектуре: транспорт (PCI на i386, mmio на aarch64)
 * выбирает libvirtio. Права на устройство выдаёт RS по random.conf - PCI
 * по идентификатору, дерево по "virtio,mmio".
 */

#include <minix/drivers.h>
#include <minix/log.h>
#include <minix/sysutil.h>
#include <minix/virtio.h>

#include <sys/mman.h>

#include <string.h>

#include "random.h"
#include "virtio_rng.h"

static struct log log = {
	.name = "virtio-rng",
	.log_level = LEVEL_INFO,
	.log_func = default_log
};

/* Идентификатор virtio-rng по спецификации. */
#define VIRTIO_RNG_ID		4

/*
 * Порция за раз. Больше, чем в TRNG (32 байта), потому что здесь каждая
 * порция стоит обхода очереди, а не чтения регистра, и потому что хост
 * отдаёт столько без задержки.
 */
#define VRNG_BUF_SIZE		64

/* Сколько ждать первую порцию при старте: 200 раз по миллисекунде. */
#define VRNG_FIRST_POLLS	200

static struct virtio_device *vrng_dev;
static u8_t *vrng_buf;
static phys_bytes vrng_buf_phys;
static int vrng_pending;

/*
 * Отдать буфер устройству. Младший бит адреса - соглашение libvirtio:
 * "этот буфер устройство заполняет", а не читает.
 */
static void
vrng_post(void)
{
	struct vumap_phys phys;

	phys.vp_addr = vrng_buf_phys | 1;
	phys.vp_size = VRNG_BUF_SIZE;
	virtio_to_queue(vrng_dev, 0, &phys, 1, vrng_buf);
	vrng_pending = 1;
}

size_t
vrng_feed(void)
{
	void *data;
	size_t len;

	if (vrng_dev == NULL || !vrng_pending)
		return 0;
	if (virtio_from_queue(vrng_dev, 0, &data, &len) != 0)
		return 0;
	vrng_pending = 0;

	if (len > VRNG_BUF_SIZE)
		len = VRNG_BUF_SIZE;
	if (len > 0) {
		random_putbytes(vrng_buf, len);
		memset(vrng_buf, 0, len);
	}
	vrng_post();
	return len;
}

int
vrng_init(void)
{
	int i, r;

	vrng_dev = virtio_setup_device(VIRTIO_RNG_ID, "virtio-rng", NULL, 0,
	    1, 0);
	if (vrng_dev == NULL) {
		log_info(&log, "no virtio-rng on this machine\n");
		return ENODEV;
	}

	if ((r = virtio_alloc_queues(vrng_dev, 1)) != OK) {
		log_warn(&log, "cannot allocate the queue: %d\n", r);
		virtio_free_device(vrng_dev);
		vrng_dev = NULL;
		return r;
	}

	vrng_buf = alloc_contig(VRNG_BUF_SIZE, AC_ALIGN4K, &vrng_buf_phys);
	if (vrng_buf == NULL) {
		log_warn(&log, "cannot allocate the buffer\n");
		virtio_free_queues(vrng_dev);
		virtio_free_device(vrng_dev);
		vrng_dev = NULL;
		return ENOMEM;
	}

	virtio_device_ready(vrng_dev);
	vrng_post();

	/*
	 * Первая порция - сейчас, а не по таймеру: с ней /dev/random посеян
	 * до первого читателя. Хост отвечает за микросекунды; предел здесь
	 * на случай устройства, которое не отвечает вовсе.
	 */
	for (i = 0; i < VRNG_FIRST_POLLS; i++) {
		if (vrng_feed() > 0) {
			log_info(&log, "seeding from virtio-rng\n");
			return OK;
		}
		micro_delay(1000);
	}
	log_warn(&log, "found, but it returned nothing\n");
	return EIO;
}
