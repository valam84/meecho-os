/* get_bp.S */
/* */
/* return the frame pointer in x0 */
/* */

#include <machine/asm.h>

ENTRY(get_bp)
	mov	x0, x29
	ret
END(get_bp)
