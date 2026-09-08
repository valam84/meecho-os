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
 * Отдельного вызова у PM нет, и он не нужен: сессия и группа процессов - это
 * у PM одно и то же поле mp_procgrp (setsid() кладёт в него pid), и
 * PM_GETSID возвращает именно его у процесса с заданным pid. То есть тот же
 * вызов отвечает и на вопрос getsid(), и на вопрос getpgid(). Если сессии
 * когда-нибудь станут отдельной сущностью, здесь понадобится свой вызов PM.
 */
pid_t getpgid(pid_t p)
{
  message m;

  memset(&m, 0, sizeof(m));
  m.m_lc_pm_getsid.pid = p;
  return(_syscall(PM_PROC_NR, PM_GETSID, &m));
}
