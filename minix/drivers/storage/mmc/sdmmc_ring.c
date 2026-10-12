/*
 * The driver's journal, kept in memory as well as printed.
 *
 * Every line that passes the log level goes to the console as before and into
 * a ring here; SDMMC_IOC_LOG copies the newest part of the ring out (see
 * sdmmc_ioc.h for why). The ring sees exactly what the console sees, so
 * asking for debug output is still the cost of asking for it - nothing is
 * formatted for the ring that would not have been printed anyway.
 */

#include <minix/drivers.h>
#include <minix/log.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "sdmmc.h"
#include "sdmmc_ioc.h"

#define RING_BYTES	16384

static char ring[RING_BYTES];
static uint64_t ring_total;

static void
ring_put(const char *s)
{
	while (*s != '\0')
		ring[ring_total++ % RING_BYTES] = *s++;
}

static void
ring_log(struct log *driver, int level, const char *UNUSED(file),
	const char *function, int line, const char *fmt, ...)
{
	char buf[320];
	va_list args;
	int n;

	if (level > driver->log_level)
		return;

	/* The function and line at debug and above, as default_log does,
	 * but not the file: it is the longest part and the least useful. */
	if (driver->log_level >= LEVEL_DEBUG)
		n = snprintf(buf, sizeof(buf), "%s(%s)%s+%d: ", driver->name,
		    level_string[level], function, line);
	else
		n = snprintf(buf, sizeof(buf), "%s(%s)", driver->name,
		    level_string[level]);
	if (n < 0 || (size_t)n >= sizeof(buf))
		n = 0;

	va_start(args, fmt);
	(void)vsnprintf(buf + n, sizeof(buf) - n, fmt, args);
	va_end(args);

	ring_put(buf);
	printf("%s", buf);
}

void
sdmmc_ring_install(struct log *log)
{
	log->log_func = ring_log;
}

int
sdmmc_ring_ioctl(endpoint_t endpt, cp_grant_id_t grant)
{
	static struct sdmmc_logdump d;
	uint64_t from, i;

	d.total = ring_total;
	d.len = (ring_total < sizeof(d.text)) ? (uint32_t)ring_total :
	    (uint32_t)sizeof(d.text);
	if (d.len > RING_BYTES)
		d.len = RING_BYTES;
	from = ring_total - d.len;
	for (i = 0; i < d.len; i++)
		d.text[i] = ring[(from + i) % RING_BYTES];

	return sys_safecopyto(endpt, grant, 0, (vir_bytes)&d, sizeof(d));
}
