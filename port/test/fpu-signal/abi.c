/* Compile-time checks for the AArch64 signal-frame FP/SIMD ABI. */

#include <stddef.h>
#include <signal.h>
#include <machine/frame.h>
#include <machine/fpu.h>

#define CTASSERT(name, test) typedef char name[(test) ? 1 : -1]

CTASSERT(fpu_state_has_all_registers,
    sizeof(struct fpu_state) == 32 * 16 + 2 * 4);
CTASSERT(sigcontext_has_complete_fpu_state,
    sizeof(((struct sigcontext *)0)->sc_fpu_state) == FPU_STATE_SIZE);
CTASSERT(fpu_state_precedes_context_metadata,
    offsetof(struct sigcontext, sc_magic) ==
    offsetof(struct sigcontext, sc_fpu_state) + FPU_STATE_SIZE);
CTASSERT(fpu_state_is_aligned_in_signal_frame,
    (offsetof(struct sigframe_sigcontext, sf_sc) +
    offsetof(struct sigcontext, sc_fpu_state)) % FPU_ALIGN == 0);
CTASSERT(signal_frame_preserves_stack_alignment,
    sizeof(struct sigframe_sigcontext) % 16 == 0);
