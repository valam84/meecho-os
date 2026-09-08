/*	at.c - fstatat, unlinkat, fchmodat, fchownat		MEECHO
 *
 * Всё семейство *at() объявлено в <sys/stat.h>, <unistd.h> и <fcntl.h> с
 * самого начала, но реализовано из него было только utimensat(2), да и тот
 * отказывает на настоящем дескрипторе каталога. Обнаружилось на OpenSSH:
 * без этих четырёх не компонуются sshd-session и sshd-auth.
 *
 * Настоящая поддержка - это разрешение пути относительно дескриптора внутри
 * VFS, то есть новый параметр у каждого вызова, который берёт путь. Здесь
 * этого нет; здесь тот же приём, которым пользуется gnulib, когда системного
 * вызова нет: перейти в каталог дескриптора, сделать обычный вызов, вернуться
 * обратно.
 *
 * Что из этого следует, и это важно:
 *
 * - Для AT_FDCWD и для абсолютного пути никакого перехода не делается, и
 *   поведение точное. Это подавляющее большинство обращений.
 * - Для настоящего дескриптора каталога вызов НЕ атомарен: между сменой
 *   каталога и обратной сменой рабочий каталог процесса другой. Программе
 *   с несколькими нитями или с сигналом, который сам ходит по путям, это
 *   видно. Одинокому процессу - нет.
 * - Если сохранить текущий каталог не удалось, вызов отказывает и никуда не
 *   переходит: испортить процессу рабочий каталог хуже, чем отказать.
 */

#include <sys/cdefs.h>
#include "namespace.h"

#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

/*
 * Перейти в каталог fd, если он вообще участвует. Возвращает 0, если можно
 * звать обычный вызов; -1 при отказе (errno выставлен). В *saved остаётся
 * дескриптор прежнего рабочего каталога или -1, если перехода не было.
 */
static int
at_enter(int fd, const char *path, int *saved)
{
	*saved = -1;

	if (path == NULL) {
		errno = EFAULT;
		return -1;
	}
	if (path[0] == '\0') {	/* требование POSIX */
		errno = ENOENT;
		return -1;
	}
	if (fd == AT_FDCWD || path[0] == '/')
		return 0;

	if ((*saved = open(".", O_RDONLY | O_CLOEXEC)) == -1)
		return -1;
	if (fchdir(fd) == -1) {
		int e = errno;
		(void)close(*saved);
		*saved = -1;
		errno = e;
		return -1;
	}
	return 0;
}

/* Вернуться назад, сохранив errno вызова, ради которого всё затевалось. */
static void
at_leave(int saved)
{
	int e = errno;

	if (saved != -1) {
		(void)fchdir(saved);
		(void)close(saved);
	}
	errno = e;
}

int
fstatat(int fd, const char *path, struct stat *sb, int flags)
{
	int saved, r;

	if ((flags & ~AT_SYMLINK_NOFOLLOW) != 0) {
		errno = EINVAL;
		return -1;
	}
	if (at_enter(fd, path, &saved) == -1)
		return -1;

	r = (flags & AT_SYMLINK_NOFOLLOW) ? lstat(path, sb) : stat(path, sb);

	at_leave(saved);
	return r;
}

int
unlinkat(int fd, const char *path, int flags)
{
	int saved, r;

	if ((flags & ~AT_REMOVEDIR) != 0) {
		errno = EINVAL;
		return -1;
	}
	if (at_enter(fd, path, &saved) == -1)
		return -1;

	r = (flags & AT_REMOVEDIR) ? rmdir(path) : unlink(path);

	at_leave(saved);
	return r;
}

int
fchmodat(int fd, const char *path, mode_t mode, int flags)
{
	int saved, r;

	if ((flags & ~AT_SYMLINK_NOFOLLOW) != 0) {
		errno = EINVAL;
		return -1;
	}
	if (at_enter(fd, path, &saved) == -1)
		return -1;

	r = (flags & AT_SYMLINK_NOFOLLOW) ? lchmod(path, mode) :
	    chmod(path, mode);

	at_leave(saved);
	return r;
}

int
fchownat(int fd, const char *path, uid_t owner, gid_t group, int flags)
{
	int saved, r;

	if ((flags & ~AT_SYMLINK_NOFOLLOW) != 0) {
		errno = EINVAL;
		return -1;
	}
	if (at_enter(fd, path, &saved) == -1)
		return -1;

	r = (flags & AT_SYMLINK_NOFOLLOW) ? lchown(path, owner, group) :
	    chown(path, owner, group);

	at_leave(saved);
	return r;
}
