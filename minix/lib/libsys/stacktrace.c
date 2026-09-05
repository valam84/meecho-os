/*
stacktrace.c

Created:	Jan 19, 1993 by Philip Homburg

Copyright 1995 Philip Homburg
*/

#include <stdio.h>
#include <string.h>
#include <minix/sysutil.h>
#include <machine/archtypes.h>

/*
 * reg_t comes from the architecture, which is the only place that knows how
 * wide a register is. This file used to declare its own as unsigned int:
 * right by accident on a 32-bit machine, and on a 64-bit one a frame pointer
 * cut in half before it was ever dereferenced.
 */
extern reg_t get_bp(void);

void util_stacktrace(void)
{
#if USE_SYSDEBUG
	reg_t bp, pc, hbp;

	bp= get_bp();
	while(bp)
	{
		pc= ((reg_t *)bp)[1];
		hbp= ((reg_t *)bp)[0];
		printf("0x%lx ", (unsigned long) pc);
		if (hbp != 0 && hbp <= bp)
		{
			printf("0x%lx ", (unsigned long) -1);
			break;
		}
		bp= hbp;
	}
	printf("\n");
#endif /* USE_SYSDEBUG */
}

