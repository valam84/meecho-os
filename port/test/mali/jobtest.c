/*
 * Стенд самопроверки службы mali: MMU ускорителя и цепочка заданий, без
 * железа.
 *
 * Собирает настоящие mali_job.c, mali_mmu.c, mali_bo.c и mali_sched.c и
 * подставляет им модель (model.c), которая знает о формате всё сама: память
 * некогерентна и разбросана, регистры исполняются в момент записи, у окон
 * свой TLB, отказ - как у железа в port/cb2-gpu/ref-fault.txt.  Самопроверка
 * с 11.6 идёт тем же путём, что задания клиентов (своё адресное
 * пространство, буфер, очередь), и стенд проверяет её так же, как в 11.5:
 * регистры окна и слота - против panfrost на плате (TRANSTAB ...007,
 * MEMATTR 0x888d88, TRANSCFG 0, JS_CONFIG 0x7b00), цепочка - словами
 * буфера, отказ - его половинами в обоих порядках.
 *
 *	sh run.sh       (собирает и этот стенд)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <minix/cachectl.h>

#include "model.h"

static void
start(void)
{
	model_reset();
	mali_sched_init();
}

/* Окно 0 включено таблицами, как у panfrost; 1, если так. */
static int
window_like_panfrost(int as, uint64_t *transtab)
{
	*transtab = ((uint64_t)G(AS_TRANSTAB_HI(as)) << 32) |
	    G(AS_TRANSTAB_LO(as));
	return (*transtab & 0xfff) == 0x007 && model_page_live(*transtab &
	    ~0xfffull) && G(AS_MEMATTR_LO(as)) == 0x888d88 &&
	    G(AS_MEMATTR_HI(as)) == 0 && G(AS_TRANSCFG_LO(as)) == 0 &&
	    model_as_active(as) == *transtab;
}

static void
test_chain(void)
{
	uint64_t transtab;
	uint32_t cmds[8];
	unsigned c0, c1;
	size_t blen;
	char *bo, want[80];
	int r, n;

	printf("цепочка заданий на модели\n");
	start();

	r = mali_job_setup();
	CHECK(r == OK, "setup: %d", r);
	bo = model_last_mmap(&blen);
	CHECK(bo != NULL && blen == 2 * PAGE, "buffer %p, %zu bytes", bo, blen);
	CHECK(covered(CACHE_CLEAN, bo, blen, 0), "the new buffer was not "
	    "cleaned: the GPU would read what the page's last owner left");
	CHECK(model_pa(bo + PAGE) != model_pa(bo) + PAGE, "the model gave "
	    "contiguous pages; it must not, to catch an assumption of them");

	c0 = ncops;
	r = mali_job_start(0);
	CHECK(r == OK, "start: %d", r);
	CHECK(model_slot_pending(1), "slot 1 not started");
	CHECK(window_like_panfrost(0, &transtab), "address space 0: TRANSTAB "
	    "0x%016llx MEMATTR 0x%08x TRANSCFG 0x%x, active 0x%llx",
	    (unsigned long long)transtab, G(AS_MEMATTR_LO(0)),
	    G(AS_TRANSCFG_LO(0)), (unsigned long long)model_as_active(0));
	CHECK(model_as_cmds(0, cmds, 1) == AS_COMMAND_UPDATE, "last address "
	    "space command %u, want UPDATE", cmds[0]);
	CHECK(G(MMU_INT_MASK) == MMU_INT_AS(0), "mmu mask 0x%x, want the "
	    "address space's two bits", G(MMU_INT_MASK));
	CHECK(G(JS_HEAD_LO(1)) == 0x02000000 && G(JS_HEAD_HI(1)) == 0,
	    "head 0x%08x%08x, panfrost's first buffer is at 0x2000000",
	    G(JS_HEAD_HI(1)), G(JS_HEAD_LO(1)));
	CHECK(G(JS_AFFINITY_NEXT_LO(1)) == 1, "affinity 0x%x",
	    G(JS_AFFINITY_NEXT_LO(1)));
	CHECK((G(JS_CONFIG(1)) & 0xffff) == 0x7b00, "config 0x%08x, the "
	    "board's first job ran with 0x7b00", G(JS_CONFIG(1)));
	CHECK(G(JOB_INT_MASK) == 0x70007, "job mask 0x%x", G(JOB_INT_MASK));
	CHECK(model_timer_pending(), "no deadline set");
	CHECK(covered(CACHE_CLEAN, bo, blen, c0),
	    "the job buffer was not cleaned before the start");
	CHECK(said("address space 0: tables at") && said("GPU address "
	    "0x2000000; TRANSTAB 0x00000000"), "window report: %s", logbuf);

	n = model_run(1);
	CHECK(n == 64, "the model ran %d job(s): %s", n, why);

	c1 = ncops;
	CHECK(model_line(MALI_IRQ_JOB) == 1, "no job line");
	CHECK(covered(CACHE_INVALIDATE, bo + PAGE, 4 * 65, c1),
	    "the buffer was read without invalidating first");
	CHECK(said("chain of 64: done in") && said("0 wrong word(s), canary "
	    "0xdeadbeef, job status first 0x1 last 0x1, 0 not done"),
	    "result: %s", logbuf);
	CHECK(said("every word checked by the processor"), "no success line");
	CHECK(!model_timer_pending(), "deadline not cancelled");
	CHECK(warnings == 0, "%u warning(s)", warnings);

	/* Второй запуск: флаг цепочки чередуется. */
	r = mali_job_start(0);
	CHECK(r == OK && (G(JS_CONFIG(1)) & 0xffff) == 0x7300,
	    "second chain: %d config 0x%x", r, G(JS_CONFIG(1)));

	/*
	 * Секунда без прерывания: слот останавливается, слово о нём; задание
	 * падает по остановке (TERMINATED), и самопроверка это говорит.
	 */
	log_clear();
	model_hang(1, 0);
	model_advance(100);
	CHECK(G(JS_COMMAND(1)) == JS_COMMAND_HARD_STOP && said("no job "
	    "interrupt within a second"), "timeout: %s", logbuf);
	snprintf(want, sizeof(want), "the chain failed: %d, slot status 0x4,",
	    ETIMEDOUT);
	CHECK(model_line(MALI_IRQ_JOB) == 1 && said(want), "after the stop: "
	    "%s", logbuf);

	/* Уход: окно пусто, память возвращена. */
	log_clear();
	mali_job_teardown();
	CHECK(G(AS_TRANSTAB_LO(0)) == 0 && G(AS_TRANSTAB_HI(0)) == 0 &&
	    G(AS_MEMATTR_LO(0)) == 0, "address space left programmed");
	CHECK(model_live_pages() == 0 && model_allocs_live() == 0,
	    "memory not given back: %u page(s)", model_live_pages());
	CHECK(model_errors == 0, "model: %s", model_error_text);
}

/* Отказ слота с кодом шейдерного ядра: слово с состоянием. */
static void
test_slot_failure(void)
{
	int r;

	printf("задание падает с состоянием слота\n");
	start();
	r = mali_job_setup();
	CHECK(r == OK, "setup: %d", r);
	model_fail(1, 0x58);
	r = mali_job_start(0);
	CHECK(r == OK, "start: %d", r);
	CHECK(model_run(1) == 0, "the model ran it");
	CHECK(model_line(MALI_IRQ_JOB) == 1, "no job line");
	CHECK(said("the chain failed: ") && said("slot status 0x58") &&
	    !said("wrong word"), "failure: %s", logbuf);
	CHECK(G(MMU_INT_MASK) == MMU_INT_AS(0), "a failure without a fault "
	    "switched the window off (mask 0x%x)", G(MMU_INT_MASK));
	mali_job_teardown();
	CHECK(model_live_pages() == 0, "memory not given back");
	CHECK(model_errors == 0, "model: %s", model_error_text);
}

/*
 * faulttest=1: цепочка, задание на неотображённый адрес, отказ по линии
 * MMU, окно выключено, задание падает, окно снова включено той же
 * самопроверке, цепочка ещё раз.  at_once - вариант железа, роняющий
 * задание сразу: обе линии в одном уведомлении, и mali.c обслуживает
 * линию заданий раньше линии MMU.
 */
static void
test_fault(int at_once)
{
	uint64_t transtab, again;
	int r, n;

	printf("отказ MMU%s\n", at_once ? ", задание падает сразу" : "");
	start();
	model_fail_at_once(at_once);

	r = mali_job_setup();
	CHECK(r == OK, "setup: %d", r);
	r = mali_job_start(1);
	CHECK(r == OK, "start: %d", r);
	CHECK(window_like_panfrost(0, &transtab), "window before");
	n = model_run(1);
	CHECK(n == 64, "the chain before the fault: %d job(s): %s", n, why);
	CHECK(model_line(MALI_IRQ_JOB) == 1, "no job line for the chain");
	CHECK(said("every word checked by the processor"), "chain: %s",
	    logbuf);

	/* Задание отказа: один дескриптор, пишет в пустую запись L3. */
	CHECK(said("one WRITE_VALUE job to GPU address 0x02010040"),
	    "no fault job: %s", logbuf);
	CHECK(model_slot_pending(1) && G(JS_HEAD_LO(1)) == 0x02000000,
	    "fault job not on the slot");
	n = model_run(1);
	CHECK(n == 0, "the fault job: %d job(s) done (%s)", n, why);
	CHECK(G(MMU_INT_RAWSTAT) == MMU_INT_PAGE_FAULT(0), "mmu raw 0x%x",
	    G(MMU_INT_RAWSTAT));
	CHECK(G(AS_FAULTADDRESS_LO(0)) == 0x02010040, "the model faulted at "
	    "0x%x (%s)", G(AS_FAULTADDRESS_LO(0)), why);

	if (at_once) {
		/* Одно уведомление: gpu, job, mmu - в этом порядке. */
		CHECK(model_line(MALI_IRQ_JOB) == 1, "no job line");
		CHECK(!said("the fault job failed") &&
		    !said("address space 0 back on"), "over before the MMU "
		    "line: %s", logbuf);
		CHECK(model_line(MALI_IRQ_MMU) == 1, "no mmu line after the "
		    "job line");
	} else {
		/* Линия MMU: выключение роняет задание и снова поднимает бит. */
		CHECK(model_line(MALI_IRQ_MMU) == 1, "no mmu line");
		CHECK(G(MMU_INT_RAWSTAT) == 0, "the line's clear does not take "
		    "the bit raised by the switch-off (0x%x)",
		    G(MMU_INT_RAWSTAT));
		CHECK((G(JOB_INT_RAWSTAT) & JOB_INT_FAILED(1)) &&
		    G(JS_STATUS(1)) == 0x48, "the job did not fail with the "
		    "window off: job raw 0x%x status 0x%x",
		    G(JOB_INT_RAWSTAT), G(JS_STATUS(1)));
		CHECK(said("address space 0 off: mmu rawstat 0x00000000 after "
		    "the clear, 0x00000001 after the switch-off"),
		    "switch-off: %s", logbuf);
		CHECK((G(MMU_INT_MASK) & MMU_INT_AS(0)) == 0, "the faulty "
		    "address space stays unmasked (0x%x), panfrost masks it",
		    G(MMU_INT_MASK));
		CHECK(!said("address space 0 back on"), "back on before the "
		    "job failed");
		CHECK(model_line(MALI_IRQ_JOB) == 1, "the job line did not "
		    "come");
	}
	CHECK(said("line 73 (mmu): status 0x00000001; address space 0 fault "
	    "status 0x7c1003c3 (translation fault 0xc3, write access, source "
	    "0x7c10) at 0x0000000002010040"), "mmu line: %s", logbuf);
	CHECK(said("line 72 (job): the fault job failed, slot status 0x48"),
	    "job line: %s", logbuf);
	CHECK(said("fault test: the fault asked for; fault status 0x7c1003c3 "
	    "(reference 0x7c1003c3), slot status 0x48 (reference 0x48), "
	    "header status 0x0 (reference 0)"), "verdict: %s", logbuf);

	/* Включено снова, теми же таблицами; цепочка после отказа. */
	CHECK(said("fault test: address space 0 back on"), "back on: %s",
	    logbuf);
	CHECK(window_like_panfrost(0, &again) && again == transtab,
	    "address space not back: TRANSTAB 0x%08x", G(AS_TRANSTAB_LO(0)));
	CHECK(G(MMU_INT_MASK) == MMU_INT_AS(0), "mmu mask 0x%x after the "
	    "fault", G(MMU_INT_MASK));
	CHECK(model_slot_pending(1), "no chain after the fault");
	n = model_run(1);
	CHECK(n == 64, "the chain after the fault: %d job(s): %s", n, why);
	CHECK(model_line(MALI_IRQ_JOB) == 1, "no job line for the chain after");
	CHECK(model_line(MALI_IRQ_MMU) == 0, "an mmu line after the fault was "
	    "over");
	CHECK(said("fault test passed: line 73 (mmu) reported the fault"),
	    "after: %s", logbuf);
	CHECK(lines_mmu == 1 && lines_job == 3, "lines: mmu %u job %u",
	    lines_mmu, lines_job);
	CHECK(warnings == 0, "%u warning(s): %s", warnings, logbuf);
	CHECK(!model_timer_pending(), "deadline not cancelled");
	mali_job_teardown();
	CHECK(model_live_pages() == 0, "memory not given back");
	CHECK(model_errors == 0, "model: %s", model_error_text);
}

/*
 * Отказ, которого никто не просил (цепочка с испорченной таблицей):
 * окно выключается, задание падает, и служба этого не скрывает - но и не
 * включает окно обратно и ничего не запускает.
 */
static void
test_unasked_fault(void)
{
	uint64_t *pte;
	int r, n;

	printf("отказ, которого никто не просил\n");
	start();
	r = mali_job_setup();
	CHECK(r == OK, "setup: %d", r);
	r = mali_job_start(0);
	CHECK(r == OK, "start: %d", r);
	/* Страница буфера пропала из таблицы (и из TLB - UPDATE его пуст). */
	pte = model_pte(0, 0x02001000);
	CHECK(pte != NULL, "no entry for the target page");
	if (pte == NULL)
		return;
	*pte = 0;
	sys_cachectl(CACHE_CLEAN, pte, 8);
	n = model_run(1);
	CHECK(n == 0 && G(MMU_INT_RAWSTAT) == MMU_INT_PAGE_FAULT(0),
	    "%d job(s), mmu raw 0x%x", n, G(MMU_INT_RAWSTAT));
	CHECK(model_line(MALI_IRQ_MMU) == 1, "no mmu line");
	CHECK(warnings >= 1 && said("line 73 (mmu)") && said("in the "
	    "self-test"), "silent: %s", logbuf);
	CHECK((model_as_active(0) & 3) != 3, "the window is still on");
	CHECK(model_line(MALI_IRQ_JOB) == 1 && said("the chain failed: ") &&
	    said("slot status 0x48") && said("its address space faulted"),
	    "job: %s", logbuf);
	CHECK(!said("fault test: one WRITE_VALUE") && !model_slot_pending(1),
	    "something started after an unasked fault");
	mali_job_teardown();
	CHECK(model_live_pages() == 0, "memory not given back");
}

int
main(int argc, char *argv[])
{
	verbose = argc > 1 && strcmp(argv[1], "-v") == 0;

	test_chain();
	test_slot_failure();
	test_fault(0);
	test_fault(1);
	test_unasked_fault();

	printf("mali jobs: %u проверок, отказов %u\n", checks, failures);
	return failures != 0;
}
