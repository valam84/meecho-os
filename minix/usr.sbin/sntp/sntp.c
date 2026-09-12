/*
 * sntp - set the clock from an NTP server, once.
 *
 * Why this exists. The board has no clock readclock(8) knows, so every boot
 * starts in 2013 (the ramdisk's fallback date), and from 2013 no TLS
 * certificate is valid yet. rdate(8) in this tree speaks the RFC 868 time
 * protocol on TCP port 37, which public servers stopped answering long ago;
 * NetBSD gets its time from ntpd, a hundred thousand lines this system does
 * not need. What it needs is one SNTP exchange (RFC 4330): a 48-byte UDP
 * request, the server's transmit timestamp in the reply, settimeofday(2).
 *
 *	sntp [-p] [-t seconds] host
 *
 *	-p	print the time, do not set it
 *	-t	how long to wait for an answer (default 5 s); each server
 *		address is tried once, so a name with several is several tries
 *
 * The step is unconditional: the offsets seen here are measured in years,
 * and adjtime(2) would take decades to slew them. Programs that care about
 * a monotonic wall clock have not started yet - this runs from rc, right
 * after the lease.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <netdb.h>
#include <netinet/in.h>

#include <err.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Seconds from the NTP era (1900) to the Unix one (1970). */
#define NTP_UNIX_OFFSET	2208988800UL

struct ntp_packet {
	uint8_t		li_vn_mode;	/* leap 0, version 4, mode 3 (client) */
	uint8_t		stratum;
	uint8_t		poll;
	int8_t		precision;
	uint32_t	root_delay;
	uint32_t	root_dispersion;
	uint32_t	reference_id;
	uint32_t	reference_ts[2];
	uint32_t	originate_ts[2];
	uint32_t	receive_ts[2];
	uint32_t	transmit_ts[2];
};

static void __dead
usage(void)
{
	fprintf(stderr, "usage: sntp [-p] [-t seconds] host\n");
	exit(2);
}

/*
 * One request to one address. Returns 0 and fills tv with the server's
 * transmit time, or -1 with the reason printed.
 */
static int
query(struct addrinfo *ai, int timeout, struct timeval *tv)
{
	struct ntp_packet req, rep;
	struct pollfd pfd;
	uint32_t secs, frac;
	ssize_t n;
	int s;

	if ((s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol)) < 0) {
		warn("socket");
		return -1;
	}
	if (connect(s, ai->ai_addr, ai->ai_addrlen) < 0) {
		warn("connect");
		close(s);
		return -1;
	}

	memset(&req, 0, sizeof(req));
	req.li_vn_mode = (4 << 3) | 3;
	/*
	 * The transmit timestamp of the request comes back as the reply's
	 * originate timestamp; a reply carrying anything else is not to this
	 * request. The value need not be the true time, only ours.
	 */
	req.transmit_ts[0] = htonl((uint32_t)time(NULL) + NTP_UNIX_OFFSET);
	req.transmit_ts[1] = htonl((uint32_t)getpid() << 16);

	if (send(s, &req, sizeof(req), 0) != sizeof(req)) {
		warn("send");
		close(s);
		return -1;
	}

	pfd.fd = s;
	pfd.events = POLLIN;
	if (poll(&pfd, 1, timeout * 1000) <= 0) {
		warnx("no answer in %d s", timeout);
		close(s);
		return -1;
	}
	n = recv(s, &rep, sizeof(rep), 0);
	close(s);
	if (n < (ssize_t)sizeof(rep)) {
		warnx("short reply (%zd bytes)", n);
		return -1;
	}
	if ((rep.li_vn_mode & 7) != 4 || rep.stratum == 0 ||
	    rep.originate_ts[0] != req.transmit_ts[0] ||
	    rep.originate_ts[1] != req.transmit_ts[1]) {
		warnx("reply is not to our request (mode %d, stratum %d)",
		    rep.li_vn_mode & 7, rep.stratum);
		return -1;
	}

	secs = ntohl(rep.transmit_ts[0]);
	frac = ntohl(rep.transmit_ts[1]);
	if (secs < NTP_UNIX_OFFSET) {
		warnx("timestamp before 1970; not trusting it");
		return -1;
	}
	tv->tv_sec = (time_t)(secs - NTP_UNIX_OFFSET);
	tv->tv_usec = (suseconds_t)((uint64_t)frac * 1000000 >> 32);
	return 0;
}

int
main(int argc, char **argv)
{
	struct addrinfo hints, *res, *ai;
	struct timeval tv;
	char buf[64];
	int c, print = 0, timeout = 5, error, got = 0;

	while ((c = getopt(argc, argv, "pt:")) != -1) {
		switch (c) {
		case 'p':
			print = 1;
			break;
		case 't':
			timeout = atoi(optarg);
			if (timeout <= 0)
				usage();
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (argc != 1)
		usage();

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	if ((error = getaddrinfo(argv[0], "ntp", &hints, &res)) != 0)
		errx(1, "%s: %s", argv[0], gai_strerror(error));

	for (ai = res; ai != NULL; ai = ai->ai_next)
		if (query(ai, timeout, &tv) == 0) {
			got = 1;
			break;
		}
	freeaddrinfo(res);
	if (!got)
		errx(1, "%s: no usable answer", argv[0]);

	if (!print && settimeofday(&tv, NULL) < 0)
		err(1, "settimeofday");
	strftime(buf, sizeof(buf), "%a %b %e %H:%M:%S %Z %Y",
	    localtime(&tv.tv_sec));
	printf("%s\n", buf);
	return 0;
}
