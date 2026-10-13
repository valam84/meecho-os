/*
 * Стенд mali: модель ускорителя, памяти и окружения службы (model.c).
 * Общая для jobtest.c (самопроверка службы) и drmtest.c (клиенты).
 */
#ifndef MODEL_H
#define MODEL_H

#include <stdint.h>
#include <time.h>

#include <minix/drivers.h>
#include <minix/chardriver.h>

#include "mali.h"
#include "malireg.h"

/* ---- проверки --------------------------------------------------------- */

extern unsigned checks, failures;
extern int verbose;

#define CHECK(cond, ...) do {						\
	checks++;							\
	if (!(cond)) {							\
		failures++;						\
		printf("  ОТКАЗ %s:%d: ", __FILE__, __LINE__);		\
		printf(__VA_ARGS__);					\
		printf("\n");						\
	}								\
} while (0)

/* ---- журнал службы ---------------------------------------------------- */

extern char logbuf[];
extern unsigned warnings;
int said(const char *what);
void log_clear(void);

/* ---- модель ----------------------------------------------------------- */

#define PAGE		4096
#define LINE		64
#define ARENA_PAGES	8192		/* 32 МиБ виртуальных и физических */
#define PHYS_BASE	0x6a000000ul	/* выдуманный физический адрес арены */
#define VFS_EP		VFS_PROC_NR
#define MODEL_SELF_EP	0x5eef		/* sef_self() службы */

extern uint32_t model_regs[];
#define G(off)		model_regs[(off) / 4]

/* Сбросить всё: память, регистры, часы, записи. */
void model_reset(void);

/* Исполнить задание, запущенное в слоте: сколько выполнено, -1 - причина в why. */
extern char why[256];
int model_run(int slot);
/* Задание дано в слот и ещё не исполнено моделью. */
int model_slot_pending(int s);
/* Слот, задание которого не завершается и HARD_STOP не берёт. */
void model_hang(int slot, int ignore_stop);
/* Задание слота падает с этим состоянием, не исполняясь. */
void model_fail(int slot, uint32_t js_status);
/* Вариант железа: задание падает сразу при отказе, обе линии вместе. */
void model_fail_at_once(int on);
/* Заданий исполнено в слоте с последнего START (и после повторов). */
unsigned model_slot_done(int s);
/* Отказ не от задания: биты MMU_INT, FAULTSTATUS, адрес. */
void model_mmu_raise(int as, uint32_t bits, uint32_t status, uint64_t va);
/* Доставить линию, как irq_handle() в mali.c: 1, если она поднята. */
int model_line(int line);
extern unsigned lines_job, lines_mmu;

/* Время: тики sys_hz() = 100; таймеры службы срабатывают. */
void model_advance(clock_t ticks);
clock_t model_now(void);
int model_timer_pending(void);

/* Память. */
uint64_t model_pa(const void *cpu);
unsigned char *model_ram(uint64_t pa, size_t len);
int model_page_live(uint64_t pa);
unsigned model_live_pages(void);
unsigned model_allocs_live(void);
/* Последнее выделение mmap(): начало и страниц. */
void *model_last_mmap(size_t *len);
/* Запись последнего уровня, которая переводит va через активные таблицы окна. */
uint64_t *model_pte(int as, uint64_t va);

/* Кэш: была ли op (или CLEAN_INVALIDATE) над всем диапазоном, начиная с from. */
extern unsigned ncops;
int covered(int op, const void *addr, size_t len, unsigned from);

/* Команды адресных пространств: последняя и история. */
uint32_t model_as_cmds(int as, uint32_t *last, unsigned n);
uint64_t model_as_active(int as);	/* TRANSTAB после последнего UPDATE */
int model_flushed(int as, uint64_t va, uint64_t len, unsigned from);
unsigned model_flush_mark(void);

/* Ошибки модели (служба нарушила правила железа или изоляции). */
extern unsigned model_errors;
extern char model_error_text[512];

/* ---- окружение: VFS, VM, процессы --------------------------------------- */

/* Процесс, от которого идёт текущий вызов (проверяет sys_vircopy). */
extern endpoint_t model_user;
extern unsigned vircopy_foreign;	/* копирований чужого процесса */

/* Грант VFS на аргумент запроса. */
cp_grant_id_t model_grant(void *buf, size_t len);

/* Отложенные ответы: был ли ответ на запрос id и какой. */
int model_reply(cdev_id_t id, int *r);
unsigned model_replies(void);

/* vm_remap(): кому, что, сколько, только ли чтение. */
struct model_remap {
	endpoint_t who;
	void *src;
	void *addr;
	size_t len;
	int ro;
};
extern struct model_remap remaps[64];
extern unsigned nremaps;

extern unsigned model_recovers;

#endif
