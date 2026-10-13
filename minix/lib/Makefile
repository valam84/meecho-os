.include <bsd.own.mk>

SUBDIR+=	libsys
SUBDIR+=	libmthread
SUBDIR+=	.WAIT

SUBDIR+=	libasyn
SUBDIR+=	libaudiodriver
SUBDIR+=	libbdev
SUBDIR+=	libblockdriver
SUBDIR+=	libchardriver
SUBDIR+=	libddekit
SUBDIR+=	libdevman
SUBDIR+=	libexec
SUBDIR+=	libfsdriver
SUBDIR+=	libinputdriver
SUBDIR+=	liblwip
SUBDIR+=	libminc
SUBDIR+=	libminixfs
SUBDIR+=	libnetdriver
SUBDIR+=	libsffs
SUBDIR+=	libsockdriver
SUBDIR+=	libsockevent
SUBDIR+=	libtimers
SUBDIR+=	libusb
SUBDIR+=	libvtreefs

# epoll, eventfd, timerfd and signalfd over poll(2): what libwayland and
# the rest of the Wayland stack of stage 11 ask for (PLAN.md, 11.3).
.if (${MACHINE_ARCH} == "aarch64")
SUBDIR+=	libepoll-shim
.endif

# Threads over libmthread under the POSIX and C11 names, <uchar.h>: what
# the same stack asks of the system and it has not got (libgfxcompat's
# Makefile says why not in libc or libmthread).
.if (${MACHINE_ARCH} == "aarch64")
SUBDIR+=	libgfxcompat
.endif

.if (${HAVE_LIBGCC} == "no")
SUBDIR+=	libgcc_s_empty
.endif

.if (${MACHINE_ARCH} == "i386")
SUBDIR+=	libacpi
SUBDIR+=	libhgfs
SUBDIR+=	libvassert
SUBDIR+=	libvboxfs
.endif

# virtio is a PC's PCI device and an AArch64 machine's device-tree node; the
# library picks the transport by architecture.
.if (${MACHINE_ARCH} == "i386") || (${MACHINE_ARCH} == "aarch64")
SUBDIR+=	libvirtio
.endif

.if (${MACHINE_ARCH} == "earm")
SUBDIR+=	libclkconf
SUBDIR+=	libgpio
SUBDIR+=	libi2cdriver
.endif

.if (${MKMAGIC} == "yes")
SUBDIR+=	libmagicrt
.endif

.include <bsd.buildinstall.mk>
.include <bsd.subdir.mk>
