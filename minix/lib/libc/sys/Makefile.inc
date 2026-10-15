.PATH:	${NETBSDSRCDIR}/minix/lib/libc/sys

MINIX_SYS_SRCS= accept.c access.c adjtime.c bind.c brk.c sbrk.c m_closefrom.c getsid.c \
	chdir.c chmod.c fchmod.c chown.c fchown.c chroot.c close.c \
	clock_getres.c clock_gettime.c clock_settime.c \
	connect.c dup.c dup2.c execve.c fcntl.c flock.c fpathconf.c fork.c \
	fstatfs.c fstatvfs.c fsync.c ftruncate.c gcov_flush_sys.c getdents.c \
	getegid.c getgid.c \
	getgroups.c getitimer.c setitimer.c __getlogin.c getpeername.c \
	getpgrp.c getpid.c getppid.c priority.c getrlimit.c getsockname.c \
	getsockopt.c setsockopt.c gettimeofday.c geteuid.c getuid.c \
	getvfsstat.c \
	ioctl.c issetugid.c kill.c link.c listen.c loadname.c lseek.c \
	memfd_create.c minix_rs.c mkdir.c mkfifo.c mknod.c mmap.c mmap_cdev.c mount.c \
	mprotect.c msync.c nanosleep.c \
	getpgid.c open.c pathconf.c pipe.c poll.c posix_fadvise.c \
	posix_spawn.c pread.c ptrace.c pwrite.c \
	read.c readlink.c reboot.c recvfrom.c recvmmsg.c recvmsg.c rename.c \
	rmdir.c sched_posix.c select.c sem.c sendmmsg.c sendmsg.c sendto.c setgroups.c setsid.c \
	setgid.c settimeofday.c setuid.c shmat.c shmctl.c shmget.c stime.c \
	vectorio.c shutdown.c sigaction.c sigpending.c sigreturn.c sigsuspend.c\
	sigprocmask.c socket.c socketpair.c stat.c statvfs.c svrctl.c \
	symlink.c \
	sync.c syscall.c truncate.c umask.c unlink.c \
	wait4.c write.c \
	utimensat.c utimes.c futimes.c lutimes.c futimens.c \
	_exit.c _ucontext.c environ.c __getcwd.c vfork.c sizeup.c init.c \
	getrusage.c setrlimit.c setpgid.c __sysctl.c

# Minix specific syscalls / utils.
MINIX_SYS_SRCS+= kernel_utils.c sprofile.c stack_utils.c _mcontext.c

SRCS+=	${MINIX_SYS_SRCS}

# The system call stubs build a 128-byte message on the stack and clear it
# first; GCC clears and copies it through q31, so every stub was an FP
# instruction, and with the kernel's lazy FP every call from a program
# that had lost the registers to someone else cost a trap - dd and cat on
# a pipe took 40000 of them in a window (O58; PORTING-LOG, "O8").  None of
# these files has arithmetic of its own.  The services are built the same
# way (minix.service.mk).
.if ${MACHINE_ARCH} == "aarch64"
.for _f in ${MINIX_SYS_SRCS}
COPTS.${_f}+=	-mgeneral-regs-only
.endfor
.endif

# Emulation for missing lchown/lchmod/lchflags
OBJS+= lchflags.o lchmod.o lchown.o
lchflags.go lchflags.o lchflags.pico lchflags.bc: ${NETBSDSRCDIR}/tools/compat/lchflags.c
lchmod.go lchmod.o lchmod.pico lchmod.bc: ${NETBSDSRCDIR}/tools/compat/lchmod.c
lchown.go lchown.o lchown.pico lchown.bc: ${NETBSDSRCDIR}/tools/compat/lchown.c

.include "${NETBSDSRCDIR}/minix/lib/libc/arch/${ARCHSUBDIR}/sys/Makefile.inc"
.include "${NETBSDSRCDIR}/minix/lib/libc/arch/${ARCHSUBDIR}/Makefile.inc"
