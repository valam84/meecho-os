/*
 * monpair.c - фигура «монитор и его непривилегированный ребёнок» без sshd.
 *
 * Зачем.  К одному и тому же sshd второе соединение не получает баннера, и
 * застревают ровно два процесса: sshd-session (монитор) и sshd-auth.  У
 * монитора в этот момент один вызов - poll(2) на ДВУХ дескрипторах: конце
 * пары AF_UNIX, по которой ребёнок просит конфигурацию, и трубе, по которой
 * ребёнок шлёт свои сообщения журнала.  Ребёнок стоит в read(2) на своём
 * конце пары.
 *
 * Отказ пропадает, если поднять уровень журнала sshd до DEBUG1 - тогда
 * ребёнок ПЕРЕД запросом пишет строку в трубу, и poll просыпается от неё.
 * То есть подозрение одно: пробуждение по данным, пришедшим в сокет
 * AF_UNIX, теряется, а труба его прикрывает.
 *
 * Эта программа повторяет фигуру и ничего больше: socketpair + pipe, fork,
 * ребёнок ЗАПУСКАЕТСЯ ЗАНОВО через exec (как sshd-auth) и первым делом
 * пишет запрос в сокет; родитель ждёт poll(-1) на сокете и трубе.  Круг
 * повторяется, и каждый круг ограничен будильником: зависший круг виден
 * как строка HUNG, а не как повисшая программа.
 *
 * Собирается кросс-компилятором против DESTDIR и кладётся в /usr/bin
 * образа:
 *   aarch64-elf64-minix-gcc -O -static -o monpair monpair.c
 *
 *   monpair [кругов]        по умолчанию 6
 *   monpair -child          внутренний режим, руками не звать
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Те же номера, что у sshd: PRIVSEP_MONITOR_FD и PRIVSEP_LOG_FD. */
#define MON_FD		4
#define LOG_FD		5

#define REQLEN		5
#define ANSLEN		4

static const char *self_path = "/usr/bin/monpair";

static void
on_alarm(int sig)
{
	(void)sig;	/* только прервать вызов */
}

/*
 * Ребёнок: то, что делает sshd-auth до баннера.  Пишет запрос в сокет и
 * ждёт ответа.  В журнал (трубу) не пишет ничего - это и есть уровень
 * журнала по умолчанию, на котором отказ воспроизводится.
 */
static int
child_main(void)
{
	char req[REQLEN], ans[ANSLEN];
	ssize_t n;

	memset(req, 'q', sizeof(req));
	n = write(MON_FD, req, sizeof(req));
	if (n != (ssize_t)sizeof(req)) {
		fprintf(stderr, "child: write %ld: %s\n", (long)n,
		    strerror(errno));
		return 1;
	}

	n = read(MON_FD, ans, sizeof(ans));
	if (n != (ssize_t)sizeof(ans)) {
		fprintf(stderr, "child: read %ld: %s\n", (long)n,
		    strerror(errno));
		return 1;
	}
	return 0;
}

/* Сколько дескрипторов слушает родитель: 2 - сокет и труба (как sshd), 1 -
 * только сокет.  Разделяет два подозрения: теряется пробуждение по сокету
 * или труба врёт про готовность. */
static int nwatch = 2;

/* Закрывает ли родитель свой конец ЗАПИСИ трубы (sshd закрывает).  Закрытие
 * конца FIFO в VFS зовёт release() - «разбудить читателей», - и делает это
 * независимо от того, остались ли у трубы другие писатели. */
static int close_wr = 1;

/* Запускается ли ребёнок заново через exec (sshd запускает sshd-auth). */
static int do_exec = 1;

static int
one_round(int round, int timeout)
{
	struct pollfd pfd[2];
	int sv[2], pp[2], status, hung = 0;
	pid_t pid;
	char req[REQLEN], ans[ANSLEN];
	ssize_t n;

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == -1) {
		printf("round %d: socketpair: %s\n", round, strerror(errno));
		return 1;
	}
	if (pipe(pp) == -1) {
		printf("round %d: pipe: %s\n", round, strerror(errno));
		return 1;
	}

	if ((pid = fork()) == -1) {
		printf("round %d: fork: %s\n", round, strerror(errno));
		return 1;
	}
	if (pid == 0) {
		char *av[3];

		close(sv[0]);
		close(pp[0]);
		if (sv[1] != MON_FD) {
			dup2(sv[1], MON_FD);
			close(sv[1]);
		}
		if (pp[1] != LOG_FD) {
			dup2(pp[1], LOG_FD);
			close(pp[1]);
		}
		if (!do_exec)
			_exit(child_main());
		av[0] = (char *)(void *)self_path;
		av[1] = (char *)(void *)"-child";
		av[2] = NULL;
		execv(self_path, av);
		fprintf(stderr, "exec %s: %s\n", self_path, strerror(errno));
		_exit(127);
	}

	/* Родитель - монитор.  Свои копии концов ребёнка ему не нужны. */
	close(sv[1]);
	if (close_wr)
		close(pp[1]);

	memset(&pfd, 0, sizeof(pfd));
	pfd[0].fd = sv[0];
	pfd[0].events = POLLIN;
	pfd[1].fd = pp[0];
	pfd[1].events = POLLIN;

	alarm(timeout);
	n = poll(pfd, nwatch, -1);
	alarm(0);

	if (n <= 0) {
		printf("round %d: HUNG - poll(%d fd) -> %ld (%s), сокет %d "
		    "труба %d\n", round, nwatch, (long)n,
		    n < 0 ? strerror(errno) : "таймаут", sv[0], pp[0]);
		hung = 1;
	} else if (!(pfd[0].revents & POLLIN)) {
		char c;
		int nb;

		/* Труба сказала "готова" - спросить её же чтением, не
		 * блокируясь: 0 значит "писателей нет" (конец файла), EAGAIN
		 * значит "пуста, писатель есть", то есть готовности не было.
		 */
		fcntl(pp[0], F_SETFL, fcntl(pp[0], F_GETFL, 0) | O_NONBLOCK);
		nb = (int)read(pp[0], &c, 1);
		printf("round %d: poll проснулся не на сокете "
		    "(revents %x/%x, сокет %d труба %d); read(труба) -> %d %s\n",
		    round, pfd[0].revents, pfd[1].revents, sv[0], pp[0], nb,
		    nb < 0 ? strerror(errno) : "");
		hung = 1;
	} else {
		alarm(timeout);
		n = read(sv[0], req, sizeof(req));
		if (n != (ssize_t)sizeof(req)) {
			printf("round %d: read %ld: %s\n", round, (long)n,
			    strerror(errno));
			hung = 1;
		} else {
			memset(ans, 'a', sizeof(ans));
			(void)write(sv[0], ans, sizeof(ans));
		}
		alarm(0);
	}

	if (hung)
		kill(pid, SIGKILL);

	close(sv[0]);
	close(pp[0]);
	if (!close_wr)
		close(pp[1]);
	(void)waitpid(pid, &status, 0);

	if (!hung)
		printf("round %d: ok\n", round);
	return hung;
}

/*
 * Самый узкий случай: труба и никого больше.  Если poll объявляет пустую
 * трубу готовой к чтению здесь, то ни сокеты, ни fork, ни exec, ни sshd к
 * делу не относятся вовсе.
 */
static int
pipe_only(void)
{
	struct pollfd p;
	struct stat st;
	int pp[2], r, bad = 0;
	pid_t pid;
	int status;

	if (pipe(pp) == -1) {
		printf("pipe: %s\n", strerror(errno));
		return 1;
	}
	if (fstat(pp[0], &st) == 0)
		printf("труба: fd %d/%d, st_mode %o, FIFO %d, st_size %lld\n",
		    pp[0], pp[1], (unsigned)st.st_mode, S_ISFIFO(st.st_mode),
		    (long long)st.st_size);

	memset(&p, 0, sizeof(p));
	p.fd = pp[0];
	p.events = POLLIN;
	r = poll(&p, 1, 1000);
	printf("1) писатель в этом же процессе: poll -> %d, revents %x%s\n",
	    r, p.revents, (r > 0) ? "   <-- ВРЁТ" : "");
	if (r > 0)
		bad++;

	/* Тот же вопрос, но писатель - в другом процессе, как у sshd. */
	if ((pid = fork()) == 0) {
		close(pp[0]);
		pause();		/* держать конец записи открытым */
		_exit(0);
	}
	close(pp[1]);

	memset(&p, 0, sizeof(p));
	p.fd = pp[0];
	p.events = POLLIN;
	r = poll(&p, 1, 1000);
	printf("2) писатель в ребёнке: poll -> %d, revents %x%s\n",
	    r, p.revents, (r > 0) ? "   <-- ВРЁТ" : "");
	if (r > 0)
		bad++;

	kill(pid, SIGKILL);
	(void)waitpid(pid, &status, 0);
	close(pp[0]);

	/* И для сравнения - конец записи закрыт совсем: тут готовность
	 * законна, это конец файла. */
	if (pipe(pp) == 0) {
		close(pp[1]);
		memset(&p, 0, sizeof(p));
		p.fd = pp[0];
		p.events = POLLIN;
		r = poll(&p, 1, 1000);
		printf("3) писателей нет (должна быть готовность): poll -> %d,"
		    " revents %x\n", r, p.revents);
		close(pp[0]);
	}

	printf("труба соврала %d раз(а) из 2\n", bad);
	return bad != 0;
}

/*
 * Ещё уже: ни fork, ни exec, ни записи.  В одном процессе по кругу
 * заводятся пара сокетов и труба, обе спрашиваются poll с коротким сроком и
 * закрываются.  Готовности взяться неоткуда, ответ должен быть 0 каждый
 * круг.  withsock == 0 - спрашивать только трубу.
 */
static int
loop_only(int rounds, int withsock)
{
	struct pollfd pfd[2];
	int sv[2], pp[2], i, r, bad = 0, n;

	for (i = 1; i <= rounds; i++) {
		if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == -1) {
			printf("socketpair: %s\n", strerror(errno));
			return 1;
		}
		if (pipe(pp) == -1) {
			printf("pipe: %s\n", strerror(errno));
			return 1;
		}

		memset(&pfd, 0, sizeof(pfd));
		if (withsock) {
			pfd[0].fd = sv[0];
			pfd[0].events = POLLIN;
			pfd[1].fd = pp[0];
			pfd[1].events = POLLIN;
			n = 2;
		} else {
			pfd[0].fd = pp[0];
			pfd[0].events = POLLIN;
			n = 1;
		}

		r = poll(pfd, n, 500);
		if (r != 0) {
			printf("круг %d (%s): poll -> %d, revents %x/%x"
			    "   <-- ВРЁТ\n", i,
			    withsock ? "сокет+труба" : "труба", r,
			    pfd[0].revents, pfd[1].revents);
			bad++;
		} else
			printf("круг %d (%s): 0, как и должно быть\n", i,
			    withsock ? "сокет+труба" : "труба");

		close(sv[0]);
		close(sv[1]);
		close(pp[0]);
		close(pp[1]);
	}
	return bad;
}

int
main(int argc, char **argv)
{
	int i, rounds = 6, bad = 0, timeout = 10;

	setvbuf(stdout, NULL, _IONBF, 0);

	if (argc > 1 && strcmp(argv[1], "-child") == 0)
		return child_main();
	if (argc > 1 && strcmp(argv[1], "-pipe") == 0)
		return pipe_only();
	if (argc > 1 && strcmp(argv[1], "-loop") == 0) {
		int n = (argc > 2) ? atoi(argv[2]) : 4;

		bad = loop_only(n, 1);
		bad += loop_only(n, 0);
		printf("соврало %d раз(а)\n", bad);
		return bad != 0;
	}
	while (argc > 1 && argv[1][0] == '-') {
		if (strcmp(argv[1], "-one") == 0)
			nwatch = 1;
		else if (strcmp(argv[1], "-nx") == 0)
			close_wr = 0;	/* не закрывать свой конец записи */
		else if (strcmp(argv[1], "-nf") == 0)
			do_exec = 0;	/* ребёнок без exec */
		else
			break;
		argc--;
		argv++;
	}
	if (argc > 1)
		rounds = atoi(argv[1]);
	if (argc > 2)
		timeout = atoi(argv[2]);

	/* Прерывать вызов, а не убивать программу. */
	signal(SIGALRM, on_alarm);
	signal(SIGPIPE, SIG_IGN);

	for (i = 1; i <= rounds; i++)
		bad += one_round(i, timeout);

	printf("кругов %d, зависло %d\n", rounds, bad);
	return bad != 0;
}
