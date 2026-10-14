#include <sys/cdefs.h>
#include "namespace.h"
#include <lib.h>

#include <string.h>
#include <unistd.h>

/*
 * MEECHO: getpgid(2) объявлен в <unistd.h> с самого начала, но реализации у
 * него не было - обнаружилось на sshd, который спрашивает getpgid(0), чтобы
 * понять, лидер ли он своей группы процессов.
 *
 * Сначала он спрашивал PM_GETSID: группа и сессия были у PM одним полем. С
 * управлением заданиями (2026-10-14) это разные вещи, и группу отвечает
 * PM_GETPGRP - тот же вызов, что у getpgrp(), только с pid. getpgrp()
 * посылает нулевое сообщение, а pid 0 - это сам вызывающий.
 */
pid_t getpgid(pid_t p)
{
  message m;

  memset(&m, 0, sizeof(m));
  m.m_lc_pm_getsid.pid = p;
  return(_syscall(PM_PROC_NR, PM_GETPGRP, &m));
}
