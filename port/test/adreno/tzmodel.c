/*
 * Модель TrustZone и вызова ядра sys_qcomscm(2) — для стадии zap
 * (port/clover/hw-gpu.md, §5 и §17).  Служба зовёт sys_qcomscm() и
 * sys_qcomscm_mem() libsys; здесь вместо них — то, что сделали бы ядро и
 * прошивка, и проверки, которых на планшете нет вовсе: там TrustZone
 * просто откажет или остановит машину, не сказав почему.
 *
 *   Ядро    состояние PAS 13 за загрузку (QCOMSCM_PAS_*); MEM_SETUP и
 *           AUTH — только после INIT, повторный INIT загруженного —
 *           EALREADY, SET_REMOTE_STATE — только загруженному.  Буфер
 *           метаданных — целиком в одном куске alloc_contig() и с начала
 *           страницы; ядро чистит его в память (CLEAN_INVALIDATE), и
 *           прошивка читает ОЗУ, не кэш.  Место образа — отображение
 *           региона zap-shader (0xf0b00000, 8 КиБ) и целиком внутри него.
 *   TZ      метаданные — байт в байт заголовки и хэш, которых ждёт стенд
 *           (tz_expect()); MEM_SETUP — размер образа; AUTH_AND_RESET —
 *           только при включённом GPU (как у Linux, из hw_init), только
 *           когда служба уже не отображает регион, и только если в регионе
 *           ровно образ (сегменты и нули до memsz).  Не тот образ —
 *           отказ (x1 не ноль), как отказал бы TrustZone на хэше.
 *   Регион  с начала — мусор 0xa5.  Отображать регион, когда прошивка
 *           уже владеет им (с начала сценария GIVEN или AUTHED), — провал:
 *           на планшете это внешний отказ доступа.
 */

#include "adreno.h"
#include "model.h"

#include <minix/cachectl.h>
#include <minix/qcomscm.h>

#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

#define ZAP_PA		0xf0b00000UL
#define ZAP_SZ		0x2000

static uint8_t zap[ZAP_SZ] __attribute__((aligned(4096)));
static const struct scenario *sc;
static int mapped;			/* the service maps the region now */
static int mem_set;			/* MEM_SETUP done this run */
static unsigned char pas13;		/* what the kernel would say */
static unsigned calls;			/* calls that reached the firmware */
static char trail[128];			/* their order */
static char refusal[128];		/* why the firmware said no */

static struct {
	const uint8_t *meta, *image;
	size_t meta_len, image_len;
	uint32_t span;
} want;

void
tz_expect(const void *meta, size_t meta_len, const void *image,
	size_t image_len, uint32_t span)
{
	want.meta = meta;
	want.meta_len = meta_len;
	want.image = image;
	want.image_len = image_len;
	want.span = span;
}

void
tz_model_reset(const struct scenario *s)
{
	sc = s;
	mapped = mem_set = 0;
	pas13 = (unsigned char)s->zap_state;
	calls = 0;
	trail[0] = refusal[0] = '\0';
	memset(zap, 0xa5, sizeof(zap));
}

unsigned tz_calls(void) { return calls; }
const char *tz_trail(void) { return trail; }
const char *tz_refusal(void) { return refusal; }
int tz_state(void) { return pas13; }

int
tz_model_same(char *why, size_t size)
{
	if (mapped) {
		snprintf(why, size, "the zap region left mapped");
		return 0;
	}
	return 1;
}

static void
step(const char *what)
{
	calls++;
	if (trail[0] != '\0')
		strlcat(trail, " ", sizeof(trail));
	strlcat(trail, what, sizeof(trail));
}

static void
refuse(struct qcomscm_answer *a, const char *why)
{
	a->res[0] = (unsigned long)-2;
	strlcpy(refusal, why, sizeof(refusal));
}

void *
tz_map(unsigned long pa, size_t len)
{
	if (pa != ZAP_PA || len > ZAP_SZ)
		return MAP_FAILED;
	if (sc->zap_state >= QCOMSCM_PAS_GIVEN)
		model_fail("the zap region mapped while TrustZone owns it "
		    "(state %d at the start)", sc->zap_state);
	mapped = 1;
	return zap;
}

int
vm_unmap_phys(int who, void *va, size_t len)
{
	(void)who;
	if (va != zap || len != ZAP_SZ || !mapped)
		model_fail("vm_unmap_phys(%p, 0x%zx): not a mapping", va, len);
	mapped = 0;
	return 0;
}

static void
answer(struct qcomscm_answer *a)
{
	a->status = 0;
	a->res[0] = a->res[1] = a->res[2] = 0;
	a->conv = QCOMSCM_CONV_ARM64;
}

int
sys_qcomscm(int op, uint32_t svc, uint32_t cmd, struct qcomscm_answer *a)
{
	if (sc->tz == 0)
		return ENODEV;
	answer(a);
	switch (op) {
	case QCOMSCM_AVAIL:
	case QCOMSCM_PAS_SUPPORTED:
		a->res[0] = 1;
		return OK;
	case QCOMSCM_PAS_STATE:
		a->res[0] = cmd == QCOMSCM_PAS_GPU ? pas13 : 0;
		return OK;
	case QCOMSCM_PAS_AUTH:
		if (cmd != QCOMSCM_PAS_GPU)
			model_fail("auth_and_reset of peripheral %u", cmd);
		if (pas13 == QCOMSCM_PAS_AUTHED)
			return EALREADY;
		if (pas13 < QCOMSCM_PAS_INITED)
			return EINVAL;
		pas13 = QCOMSCM_PAS_GIVEN;
		step("auth");
		if (!model_gpu_powered())
			model_fail("auth_and_reset with the GPU unpowered");
		if (mapped)
			model_fail("auth_and_reset while the service still maps "
			    "the region");
		if (!mem_set)
			refuse(a, "no mem_setup for a relocatable image");
		else if (memcmp(zap, want.image, want.image_len) != 0)
			refuse(a, "the region is not the image");
		else if (sc->zap_auth_fail)
			refuse(a, "the scenario's signature check");
		else
			pas13 = QCOMSCM_PAS_AUTHED;
		return OK;
	case QCOMSCM_REMOTE_STATE:
		if (svc != QCOMSCM_STATE_RESUME || cmd != QCOMSCM_PAS_GPU ||
		    pas13 != QCOMSCM_PAS_AUTHED)
			return EINVAL;
		step("remote");
		if (!model_gpu_powered())
			model_fail("set_remote_state with the GPU unpowered");
		return OK;
	}
	return EINVAL;
}

int
sys_qcomscm_mem(int op, uint32_t id, void *addr, size_t len,
	struct qcomscm_answer *a)
{
	const uint8_t *ram;
	uint64_t pa;

	if (sc->tz == 0)
		return ENODEV;
	if (id != QCOMSCM_PAS_GPU)
		model_fail("pas call %d for peripheral %u", op, id);
	answer(a);
	switch (op) {
	case QCOMSCM_PAS_INIT:
		if (pas13 == QCOMSCM_PAS_AUTHED)
			return EALREADY;
		if (((uintptr_t)addr & 4095) != 0 || len == 0 ||
		    len > QCOMSCM_META_MAX ||
		    model_ram_of(addr, len, &pa) == NULL)
			return EINVAL;
		/* The kernel's cleaning; then the firmware reads memory. */
		(void)sys_cachectl(CACHE_CLEAN_INVALIDATE, addr, len);
		ram = model_ram_of(addr, len, &pa);
		step("init");
		if (len != want.meta_len || memcmp(ram, want.meta, len) != 0)
			refuse(a, "the metadata is not the headers and hash");
		else if (pas13 < QCOMSCM_PAS_INITED)
			pas13 = QCOMSCM_PAS_INITED;
		return OK;
	case QCOMSCM_PAS_MEM_SETUP:
		if (pas13 == QCOMSCM_PAS_AUTHED)
			return EALREADY;
		if (pas13 < QCOMSCM_PAS_INITED)
			return EINVAL;
		if (addr != zap || !mapped || len == 0 || len > ZAP_SZ)
			return EINVAL;
		pas13 = QCOMSCM_PAS_GIVEN;
		step("mem");
		if (len != want.span)
			refuse(a, "mem_setup of a size not the image's");
		else
			mem_set = 1;
		return OK;
	}
	return EINVAL;
}
