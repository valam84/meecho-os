# $NetBSD: Makefile,v 1.26 2015/08/21 10:44:43 roy Exp $
#

PROG=		dhcpcd
SRCS=		common.c control.c dhcpcd.c duid.c eloop.c
SRCS+=		if.c if-options.c script.c
SRCS+=		dhcp-common.c dhcpcd-embedded.c
SRCS+=		if-bsd.c

WARNS?=		6
USE_FORT?=	yes	# network client (local server)

CPPFLAGS+=	-DHAVE_CONFIG_H

.include <bsd.own.mk>

.if defined(__MINIX)
CWARNFLAGS.gcc+=	-Wno-maybe-uninitialized
.endif # defined(__MINIX)

#
# dhcpcd 6.9.3 is an import, and this is the mechanism NetBSD itself uses
# for imports a modern compiler has caught up with: one named diagnostic in
# one named file, never a blanket -w.  What each of these is, so that a
# later import can be checked against it rather than have the list copied
# forward blindly:
#
# if.c, dhcp6.c - a switch case that falls into the next with no comment
#	saying so.  GCC 7 wants the comment; the code is from before it
#	asked.
# dhcp6.c - the address of a field of a packed structure, which on a
#	machine that faults on unaligned access would be a real defect and
#	on this one is not: AArch64 does unaligned loads in normal memory.
# dhcp6.c - a "%s" whose argument GCC can prove is NULL on one path
#	through a log call.  A real hole, and one this system does not fall
#	into: its vfprintf prints "(null)" rather than dereferencing.
# if-bsd.c - a four-byte read past the end of a sockaddr union.  This one
#	is a genuine defect and worth writing down: ADDSU copies
#	RT_ROUNDUP(sa_len) bytes out of a union whose largest member is a
#	20-byte sockaddr_dl, and RT_ROUNDUP rounds to eight, so the
#	sockaddr_dl case copies twenty-four.  The four extra bytes come off
#	the same stack frame and go into the padding of a routing message
#	that the receiver ignores, which is why it has never shown up as
#	anything.  Upstream fixed it much later by giving the union a
#	sockaddr_storage member; that is an import away, not a local patch.
#	GCC reports it under two names - as an out-of-bounds offset and as
#	an over-read of a known size ("reading 24 bytes from a region of
#	size 20", which is exactly the arithmetic above) - so both are
#	named here rather than one of them being found later by surprise.
#
COPTS.if.c+=		${CC_WNO_IMPLICIT_FALLTHROUGH}
COPTS.dhcp6.c+=		${CC_WNO_IMPLICIT_FALLTHROUGH} \
			${CC_WNO_ADDRESS_OF_PACKED_MEMBER} \
			${CC_WNO_FORMAT_OVERFLOW}
COPTS.if-bsd.c+=	${CC_WNO_ARRAY_BOUNDS} ${CC_WNO_STRINGOP_OVERREAD}

SRCS+=		auth.c hmac_md5.c

USE_INET?=	yes
.if (${USE_INET} != "no")
CPPFLAGS+=	-DINET
SRCS+=		arp.c dhcp.c ipv4.c ipv4ll.c
.endif

.if (${USE_INET6} != "no")
CPPFLAGS+=	-DINET6
SRCS+=		ipv6.c ipv6nd.c dhcp6.c
.endif

DIST=		${NETBSDSRCDIR}/external/bsd/dhcpcd/dist
CPPFLAGS+=	-I${DIST}

.PATH: ${DIST} ${DIST}/crypt ${LIBC_NET}

SCRIPTS=			dhcpcd-run-hooks
SCRIPTSDIR_dhcpcd-run-hooks=	/libexec

CONFIGFILES=			dhcpcd.conf
FILESDIR_dhcpcd.conf=		/etc

HOOKS=			01-test 02-dump 10-wpa_supplicant 15-timezone
HOOKS+=			20-resolv.conf 29-lookup-hostname 30-hostname
HOOKS+=			50-ntp.conf

FILES=			${HOOKS:C,^,${DIST}/dhcpcd-hooks/,}
FILESDIR=		/libexec/dhcpcd-hooks

MAN=			dhcpcd.conf.5 dhcpcd.8 dhcpcd-run-hooks.8

CLEANFILES=		dhcpcd.conf.5 dhcpcd.8 \
			dhcpcd-run-hooks dhcpcd-run-hooks.8

.for f in dhcpcd-run-hooks dhcpcd.conf.5 dhcpcd.8 dhcpcd-run-hooks.8
${f}:	${f}.in
	${TOOL_SED} -e 's:@SYSCONFDIR@:/etc:g' -e 's:@DBDIR@:/var/db:g' \
	    -e 's:@LIBDIR@:/lib:g' \
	    -e 's:@RUNDIR@:/var/run:g' \
	    -e 's:@HOOKDIR@:/libexec/dhcpcd-hooks:g' \
	    -e 's:@SCRIPT@:/libexec/dhcpcd-run-hooks:g' \
	    -e 's:@SERVICEEXISTS@::g' \
	    -e 's:@SERVICECMD@::g' \
	    -e 's:@SERVICESTATUS@::g' \
	    ${DIST}/${f}.in > $@
.endfor

.include <bsd.prog.mk>
