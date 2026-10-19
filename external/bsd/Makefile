#	$NetBSD: Makefile,v 1.54 2015/09/28 17:12:46 christos Exp $

.include <bsd.own.mk>

#MINIX:
SUBDIR=	bind byacc dhcpcd \
	fetch file flex less \
	libarchive libevent libpcap mdocml \
	openresolv tcpdump tmux top

.if (${MKATF} != "no")
SUBDIR+= atf
.endif
.if (${MKCRYPTO} != "no")
SUBDIR+= pkg_install
.endif
# IP Filter
.if (${MKIPFILTER} != "no")
SUBDIR+=ipf
.endif
.if (${MKISCSI} != "no")
SUBDIR+= iscsi 
.endif
.if (${MKKYUA} != "no")
SUBDIR+= lutok kyua-testers kyua-cli kyua-atf-compat
.endif
.if (${MKLDAP} != "no")
SUBDIR+= openldap
.endif
.if (${MKLIBCXX} != "no")
SUBDIR+= libc++
.endif
.if (${MKLLVM} != "no")
.if (${MKLLVMCMDS} != "no") && defined(__MINIX)
SUBDIR+= llvm
.endif # (${MKLLVMCMDS} != "no") && defined(__MINIX)
.endif
.if (${MKPCC} != "no")
SUBDIR+= pcc
.endif
.if (${MKDTRACE} != "no")
SUBDIR+= libproc
SUBDIR+= librtld_db
.endif

#MINIX:
SUBDIR+= blacklist dhcp nvi

# The desktop (stage 11.7): the file manager, whose threads are
# libgfxcompat's (external/mit has the rest of the desktop, external/ofl
# its typeface).
.if ${MACHINE_ARCH} == "aarch64"
SUBDIR+= nnn
.endif

# A Qualcomm modem's userland (port/clover/hw-modem.md): libqrtr, then the
# daemons on it the modem needs - its storage, its files, its protection
# domains' locator and the taker of its diagnostic stream.
.if ${MACHINE_ARCH} == "aarch64"
SUBDIR+= qrtr .WAIT rmtfs tqftpserv pd-mapper diag
.endif

.include <bsd.subdir.mk>
