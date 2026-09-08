/* FPU state corruption test. This used to be able to crash the kernel. */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <machine/fpu.h>

int max_error = 1;
#include "common.h"


double state = 2.0;
static int count;

static void use_fpu(int n)
{
  state += (double) n * 0.5;
}

static void crashed(int sig)
{
  exit(EXIT_SUCCESS);
}

static void handler(int sig, int code, struct sigcontext *sc)
{
  memset(&sc->sc_fpu_state, count, sizeof(sc->sc_fpu_state));
}

#if defined(__aarch64__)
#define FPCR_ROUND_PLUS_INFINITY	(1UL << 22)
#define FPCR_ROUND_MINUS_INFINITY	(2UL << 22)

static volatile unsigned long handler_fpcr;

static unsigned long read_fpcr(void)
{
  unsigned long value;

  __asm__ volatile("mrs %0, fpcr" : "=r"(value));
  return value;
}

static void write_fpcr(unsigned long value)
{
  __asm__ volatile("msr fpcr, %0" :: "r"(value));
}

static void fpcr_handler(int sig, int code, struct sigcontext *sc)
{
  handler_fpcr = read_fpcr();
  write_fpcr(FPCR_ROUND_MINUS_INFINITY);
}
#endif

int main(void)
{
  int status;

  start(62);
  subtest = 0;

  signal(SIGUSR1, (void (*)(int)) handler);

  /* Initialize the FPU state. This state is inherited, too. */
  use_fpu(-1);

  for (count = 0; count <= 255; count++) {
	switch (fork()) {
	case -1:
		e(1);

		break;

	case 0:
		signal(SIGFPE, crashed);

		/* Load bad state into the kernel. */
		if (kill(getpid(), SIGUSR1)) e(2);

		/* Let the kernel restore the state. */
		use_fpu(count);

		exit(EXIT_SUCCESS);

	default:
		/* We cannot tell exactly whether what happened is correct or
		 * not -- certainly not in a platform-independent way. However,
		 * if the whole system keeps running, that's good enough.
		 */
		(void) wait(&status);
	}
  }

#if defined(__aarch64__)
  subtest = 1;
  signal(SIGUSR1, (void (*)(int)) fpcr_handler);

  /*
   * The handler must begin with the architectural default, not the state it
   * interrupted. Its own changes must then disappear at sigreturn.
   */
  write_fpcr(FPCR_ROUND_PLUS_INFINITY);
  if (kill(getpid(), SIGUSR1)) e(4);
  if (handler_fpcr != 0) e(5);
  if (read_fpcr() != FPCR_ROUND_PLUS_INFINITY) e(6);
  write_fpcr(0);
#endif

  if (state <= 1.4 || state >= 1.6) e(3);

  quit();

  return 0;
}
