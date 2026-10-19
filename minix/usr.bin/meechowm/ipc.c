/*
 * Commands over a socket: "meechowm -m focus right" sends a line of text,
 * the compositor runs it as it runs a key's (strip.c, wm_command()) and
 * answers with the command's reply, then closes the connection.  This is
 * how a launcher, a script or a stand steers the strip.
 *
 * The socket is $MEECHOWM_SOCK, or meechowm.sock in $XDG_RUNTIME_DIR -
 * where the Wayland socket is, a directory of the user's own (0700), and
 * the socket is made 0600 besides: only the one who started the compositor
 * steers it.
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <wayland-server-core.h>
#include <wlr/util/log.h>

#include "wm.h"

struct ipc_conn {
	struct tinywl_server *server;
	struct wl_event_source *source;
	int fd;
	size_t len;
	char line[1024];
};

static char sock_path[sizeof(((struct sockaddr_un *)0)->sun_path)];

static bool
socket_path(void)
{
	const char *p = getenv("MEECHOWM_SOCK"), *dir;
	int n;

	if (p != NULL)
		n = snprintf(sock_path, sizeof(sock_path), "%s", p);
	else if ((dir = getenv("XDG_RUNTIME_DIR")) != NULL)
		n = snprintf(sock_path, sizeof(sock_path), "%s/meechowm.sock",
		    dir);
	else {
		fprintf(stderr, "meechowm: neither MEECHOWM_SOCK nor "
		    "XDG_RUNTIME_DIR is set\n");
		return false;
	}
	if (n < 0 || (size_t)n >= sizeof(sock_path)) {
		fprintf(stderr, "meechowm: socket path too long\n");
		return false;
	}
	return true;
}

/* Write all of it, the socket made blocking for that: replies are short. */
static void
write_all(int fd, const char *p, size_t len)
{
	ssize_t r;

	(void)fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) & ~O_NONBLOCK);
	while (len > 0) {
		if ((r = write(fd, p, len)) < 0) {
			if (errno == EINTR)
				continue;
			return;
		}
		p += r;
		len -= (size_t)r;
	}
}

static void
conn_close(struct ipc_conn *c)
{
	wl_event_source_remove(c->source);
	close(c->fd);
	free(c);
}

static int
conn_readable(int fd, uint32_t mask, void *data)
{
	static struct mw_reply reply;
	struct ipc_conn *c = data;
	ssize_t r = 0;

	if (mask & WL_EVENT_READABLE) {
		r = read(fd, c->line + c->len, sizeof(c->line) - 1 - c->len);
		if (r < 0 && (errno == EAGAIN || errno == EINTR))
			return 0;
	}
	if (r > 0) {
		c->len += (size_t)r;
		if (memchr(c->line, '\n', c->len) == NULL &&
		    c->len < sizeof(c->line) - 1)
			return 0;		/* the rest of the line */
	}
	if (c->len == 0) {			/* gone without a word */
		conn_close(c);
		return 0;
	}
	c->line[c->len] = '\0';
	memset(&reply, 0, sizeof(reply));
	wm_command(c->server, c->line, &reply);
	write_all(fd, reply.buf, reply.len);
	conn_close(c);
	return 0;
}

static int
listen_readable(int fd, uint32_t mask, void *data)
{
	struct tinywl_server *server = data;
	struct ipc_conn *c;
	int cfd;

	if ((cfd = accept(fd, NULL, NULL)) < 0)
		return 0;
	if ((c = calloc(1, sizeof(*c))) == NULL) {
		close(cfd);
		return 0;
	}
	(void)fcntl(cfd, F_SETFD, FD_CLOEXEC);
	(void)fcntl(cfd, F_SETFL, fcntl(cfd, F_GETFL) | O_NONBLOCK);
	c->server = server;
	c->fd = cfd;
	c->source = wl_event_loop_add_fd(
	    wl_display_get_event_loop(server->wl_display), cfd,
	    WL_EVENT_READABLE, conn_readable, c);
	if (c->source == NULL) {
		close(cfd);
		free(c);
	}
	return 0;
}

bool
ipc_start(struct tinywl_server *server)
{
	struct sockaddr_un sun;
	int fd;

	if (!socket_path())
		return false;
	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_UNIX;
	strlcpy(sun.sun_path, sock_path, sizeof(sun.sun_path));
	if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
		wlr_log_errno(WLR_ERROR, "ipc: socket");
		return false;
	}
	(void)fcntl(fd, F_SETFD, FD_CLOEXEC);
	(void)fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
	(void)unlink(sock_path);
	if (bind(fd, (struct sockaddr *)&sun, sizeof(sun)) < 0 ||
	    chmod(sock_path, 0600) < 0 || listen(fd, 8) < 0) {
		wlr_log_errno(WLR_ERROR, "ipc: %s", sock_path);
		close(fd);
		return false;
	}
	if (wl_event_loop_add_fd(wl_display_get_event_loop(server->wl_display),
	    fd, WL_EVENT_READABLE, listen_readable, server) == NULL) {
		close(fd);
		return false;
	}
	wlr_log(WLR_INFO, "commands: %s", sock_path);
	return true;
}

void
ipc_stop(void)
{
	if (sock_path[0] != '\0')
		(void)unlink(sock_path);
}

/* meechowm -m WORD...: the words as one command, the reply to stdout. */
int
ipc_send(int argc, char **argv)
{
	struct sockaddr_un sun;
	char line[1024], buf[4096];
	size_t len = 0;
	ssize_t r;
	int fd, i, n, failed = -1;

	if (argc < 1) {
		fprintf(stderr, "usage: meechowm -m command [argument ...]\n");
		return 2;
	}
	for (i = 0; i < argc; i++) {
		n = snprintf(line + len, sizeof(line) - len, "%s%s",
		    i > 0 ? " " : "", argv[i]);
		if (n < 0 || (size_t)n >= sizeof(line) - len - 1) {
			fprintf(stderr, "meechowm: command too long\n");
			return 2;
		}
		len += (size_t)n;
	}
	line[len++] = '\n';

	if (!socket_path())
		return 2;
	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_UNIX;
	strlcpy(sun.sun_path, sock_path, sizeof(sun.sun_path));
	if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0 ||
	    connect(fd, (struct sockaddr *)&sun, sizeof(sun)) < 0) {
		fprintf(stderr, "meechowm: %s: %s\n", sock_path,
		    strerror(errno));
		return 2;
	}
	write_all(fd, line, len);
	while ((r = read(fd, buf, sizeof(buf))) > 0 ||
	    (r < 0 && errno == EINTR)) {
		if (r <= 0)
			continue;
		if (failed < 0)
			failed = (size_t)r >= 6 && memcmp(buf, "error:", 6) == 0;
		fwrite(buf, 1, (size_t)r, stdout);
	}
	close(fd);
	if (failed < 0) {
		fprintf(stderr, "meechowm: no reply\n");
		return 2;
	}
	return failed;
}

/*
 * A command and its reply, for a client in this program (barstat.c): the
 * reply's length in buf (at most size - 1, the rest dropped), or -1 when
 * the compositor is not there.
 */
int
ipc_query(const char *line, char *buf, size_t size)
{
	struct sockaddr_un sun;
	size_t len = 0;
	ssize_t r;
	int fd;
	char drop[512];

	if (!socket_path())
		return -1;
	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_UNIX;
	strlcpy(sun.sun_path, sock_path, sizeof(sun.sun_path));
	if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
		return -1;
	if (connect(fd, (struct sockaddr *)&sun, sizeof(sun)) < 0) {
		close(fd);
		return -1;
	}
	write_all(fd, line, strlen(line));
	for (;;) {
		if (len < size - 1)
			r = read(fd, buf + len, size - 1 - len);
		else
			r = read(fd, drop, sizeof(drop));
		if (r < 0 && errno == EINTR)
			continue;
		if (r <= 0)
			break;
		if (len < size - 1)
			len += (size_t)r;
	}
	close(fd);
	return (int)len;
}
