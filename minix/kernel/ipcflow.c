/*
 * ipcflow: the matrix itself.  See <minix/ipcflow.h> for what a cell
 * counts, and for why the cells are plain globals and not per-CPU.
 */

#include <string.h>
#include <minix/minlib.h>	/* read_tsc_64() */

#include "kernel/kernel.h"
#include "kernel/proc.h"
#include "kernel/proto.h"
#include "kernel/clock.h"	/* ms_2_cpu_time() */
#include "kernel/ipcflow.h"

#if IPCFLOW

/*
 * A row per slot of the process table, a column per slot and one more for
 * the processes that have gone.  Half a megabyte, and that is the honest
 * size: any process may send to any other it is allowed to, and a table
 * that kept only the busiest pairs would lose exactly the sums the x-ray
 * view is checked by.
 */
uint64_t ipcflow_cell[IF_NPROC][IF_NPROC + 1];
uint64_t ipcflow_kernel[IF_NPROC];

/*===========================================================================*
 *				ipcflow_clear				     *
 *===========================================================================*/
/*
 * A process is going (clear_endpoint()): what others sent it moves to
 * their "gone" column, and its own row goes with it.  Afterwards the slot
 * is as a fresh one, so the next tenant starts from nothing, and no row's
 * sum has gone down.  Nothing is counted to or from the slot from here on:
 * mini_send() refuses a receiver with RTS_NO_ENDPOINT, which the caller
 * has set, and isokendpt() one whose slot is free.
 */
void
ipcflow_clear(const struct proc *rc)
{
	int s = rc->p_nr + NR_TASKS, i;

	for (i = 0; i < IF_NPROC; i++) {
		ipcflow_cell[i][IF_GONE] += ipcflow_cell[i][s];
		ipcflow_cell[i][s] = 0;
	}
	memset(ipcflow_cell[s], 0, sizeof(ipcflow_cell[s]));
	ipcflow_kernel[s] = 0;
}

/*===========================================================================*
 *				ipcflow_row				     *
 *===========================================================================*/
/*
 * The row of endpoint e, for GET_IPCFLOW.  The columns' tenants are taken
 * here too, under the same lock as the counts: a reader that joined the
 * counts with a process table of its own would be joining two moments.
 */
int
ipcflow_row(endpoint_t e, struct ipcflow *row)
{
	struct proc *rp;
	int p, s, i;

	if (!isokendpt(e, &p))
		return EINVAL;
	s = p + NR_TASKS;

	row->if_version = IPCFLOW_VERSION;
	row->if_freq = ms_2_cpu_time(1000);
	read_tsc_64(&row->if_tsc);
	row->if_slot = s;
	row->if_endpoint = e;
	row->if_kernel = ipcflow_kernel[s];
	for (i = 0; i < IF_NPROC; i++) {
		rp = &proc[i];
		if (isemptyp(rp)) {
			row->if_dst[i] = NONE;
			row->if_name[i][0] = '\0';
			continue;
		}
		row->if_dst[i] = rp->p_endpoint;
		/*
		 * Bounded on the source too: a name of full length fills
		 * p_name with no terminator (as do_getinfo.c says of KTRACE).
		 */
		memcpy(row->if_name[i], rp->p_name, IF_NAMELEN - 1);
		row->if_name[i][IF_NAMELEN - 1] = '\0';
	}
	memcpy(row->if_count, ipcflow_cell[s], sizeof(row->if_count));
	return OK;
}

#endif /* IPCFLOW */
