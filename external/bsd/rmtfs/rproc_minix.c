/*
 * MEECHO: rmtfs's -s, which starts and stops the modem through Linux's
 * remoteproc sysfs (dist/rproc.c), has no counterpart here: the qcom_mss
 * service starts the modem, and rmtfs runs beside it.  -s says so and
 * fails.
 */

#include <stdio.h>

#include "rmtfs.h"

int
rproc_init(void)
{
	fprintf(stderr, "rmtfs: -s is not for MEECHO; qcom_mss starts the "
	    "modem\n");
	return -1;
}

int
rproc_start(void)
{
	return -1;
}

int
rproc_stop(void)
{
	return -1;
}
