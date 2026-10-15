# MINIX-specific servers/drivers options
.include <bsd.own.mk>

# LSC: Our minimal c library has no putchar, which is called by the builtin
#      functions of the compiler, so prevent using them.

AFLAGS+= -D__ASSEMBLY__
COPTS+= -fno-builtin

# For MKCOVERAGE builds, enable coverage options.
.if ${MKCOVERAGE:Uno} == "yes"
CPPFLAGS+= ${COVCPPFLAGS}
LDADD+= ${COVLDADD}
.endif # ${MKCOVERAGE:Uno} == "yes"

# LSC Static linking, order matters!
# We can't use --start-group/--end-group as they are not supported by our
# version of clang.

# 1. No default libs
LDADD:= -nodefaultlibs ${LDADD}

# 2. Services system library
LDADD+= -lsys
DPADD+= ${LIBSYS}

# 3. Minimal C library, if libc had not yet been added
.if ${LDADD:M-lc} == ""
LDADD+= -lminc
DPADD+= ${LIBMINC}
.endif # empty(${LDADD:M-lc})

.if ${MACHINE_ARCH} == "earm"

# LSC: On ARM, when compiling statically, with gcc, lgcc_eh is required
.if ${PROG:U} != "kernel" && !empty(CC:M*gcc)
# gcc_eh uses abort(), which is provided by minc
LDFLAGS+= ${${ACTIVE_CC} == "gcc":? -lgcc_eh:}
.endif # ${PROG:U} != "kernel" && !empty(CC:M*gcc)

.endif # ${MACHINE_ARCH} == "earm"

# Get (more) internal minix definitions and declarations.
CPPFLAGS += -D_MINIX_SYSTEM=1

# AArch64: system services do not touch FP/SIMD registers. The kernel switches
# them lazily - the first FP instruction after a context switch traps and the
# registers change hands - and a service has no arithmetic that needs them;
# what made every service trap was GCC zeroing and copying the 128-byte
# message through q31. With VFS, a driver and the shell each taking a trap on
# every switch, the trap was a tenth of the kernel's instructions on an IPC
# load (port/PORTING-LOG.md, "О8"). The service libraries in minix/lib are
# built the same way (minix/lib/Makefile.inc), and libminc's getcontext()
# relies on it to leave d8-d15 alone (ucontext.S); port/fp-free.sh checks
# the installed services. A service that really computes in floating point
# sets SERVICE_FP=yes before including this file.
.if ${MACHINE_ARCH} == "aarch64" && ${SERVICE_FP:Uno} != "yes"
CFLAGS+=	-mgeneral-regs-only
.endif

# For MKMAGIC builds, link services against libmagicrt and run the magic pass
# on them, unless they have specifically requested to be built without bitcode.
.if ${USE_BITCODE:Uno} == "yes" && ${USE_MAGIC:Uno} == "yes"
LIBMAGICST?= ${DESTDIR}${LIBDIR}/libmagicrt.bcc
MAGICPASS?= ${NETBSDSRCDIR}/minix/llvm/bin/magic.so

DPADD+= ${LIBMAGICST} ${MAGICPASS}

.for _P in ${PROGS:U${PROG}}
BITCODE_LD_FLAGS_1ST.${_P}?= ${LIBMAGICST}
.endfor

MAGICFLAGS?=
OPTFLAGS+= -load ${MAGICPASS} -magic ${MAGICFLAGS}

# For MKASR builds, generate an additional set of rerandomized service
# binaries.
.if ${USE_ASR:Uno} == "yes"
ASRPASS?= ${NETBSDSRCDIR}/minix/llvm/bin/asr.so
ASRCOUNT?= 3
ASRDIR?= /usr/service/asr

DPADD+= ${ASRPASS}

OPTFLAGS+= -load ${ASRPASS} -asr

# Produce a variable _RANGE that contains "1 2 3 .. ${ASRCOUNT}".  We do not
# want to invoke a shell command to do this; what if the host platform does not
# have seq(1) ?  So, we do it with built-in BSD make features instead.  There
# are probably substantially better ways to do this, though.  Right now the
# maximum ASRCOUNT is 65536 (16**4), which should be plenty.  An ASRCOUNT of 0
# is not supported, nor would it be very useful.
_RANGE= 0
_G0= xxxxxxxxxxxxxxxx
_G= ${_G0:S/x/${_G0}/g:S/x/${_G0}/g:S/x/${_G0}/g}
.for _X in ${_G:C/^(.{${ASRCOUNT}}).*/\1/:S/x/x /g}
_RANGE:= ${_RANGE} ${_RANGE:[#]}
.endfor
_RANGE:= ${_RANGE:[2..-1]}

# Add progname-1, progname-2, progname-3 (etc) to the list of programs to
# generate, and install (just) these to ASRDIR.
PROGS?= ${PROG}
_PROGLIST:= ${PROGS}
.for _N in ${_RANGE}
.for _P in ${_PROGLIST}
PROGS+= ${_P}-${_N}
SRCS.${_P}-${_N}= ${SRCS.${_P}:U${SRCS}}
BITCODE_LD_FLAGS_1ST.${_P}-${_N}:= ${BITCODE_LD_FLAGS_1ST.${_P}}
BINDIR.${_P}-${_N}= ${ASRDIR}
.endfor
.endfor

.endif # ${USE_ASR:Uno} == "yes"
.endif # ${USE_BITCODE:Uno} == "yes" && ${USE_MAGIC:Uno} == "yes"

.include <bsd.prog.mk>
