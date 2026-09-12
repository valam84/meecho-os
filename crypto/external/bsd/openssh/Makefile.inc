#	$NetBSD: Makefile.inc,v 1.21 2026/07/28 23:49:07 mrg Exp $

WARNS?=	4

.include <bsd.own.mk>

USE_FORT?= yes	# network client/server

# Avoid increasing the binary size by not linking with libcrypto
# on static platforms.
#
# MEECHO: until 2026-09-12 this took the no-libcrypto branch (the one
# NetBSD uses on m68000): the libcrypto in the tree was OpenSSL 1.0.1p and
# did not build.  With OpenSSL 3.5 in crypto/external/apache2 there is no
# reason left - RSA and ECDSA host keys, DH-GEX and the wider cipher list
# come back.  PKCS#11 stays out: it wants dlopen, and the userland here is
# static (see HAVE_DLOPEN below).
.if ${MACHINE_ARCH} != "m68000"
WITH_OPENSSL=1
.endif

SSHDIST?= ${NETBSDSRCDIR}/crypto/external/bsd/openssh/dist

CPPFLAGS+=-I${SSHDIST}

CPPFLAGS+=-D_OPENBSD_SOURCE
CPPFLAGS+=-DHAVE_HEADER_AD
CPPFLAGS+=-DHAVE_STDLIB_H
.if !defined(__MINIX)
# MEECHO: динамической загрузки нет - ld.elf_so ещё не запускается, весь
# userland статический (см. CLAUDE.md, веха 8.3).
CPPFLAGS+=-DHAVE_DLOPEN
.endif
# MEECHO: login_cap в libutil есть целиком, включая setusercontext (в
# библиотеке он под versioned-именем __setusercontext50). /etc/login.conf в
# системе нет, но login_getclass() это переживает - как и на NetBSD. Без
# этого определения не собирается session.c: в нём одно обращение к lc
# (LOGIN_SETPATH) стоит вообще без #ifdef.
CPPFLAGS+=-DHAVE_LOGIN_CAP
.if defined(HAVE_OPENSSL) && ${HAVE_OPENSSL} < 11
CPPFLAGS+=-DOPENSSL_API_COMPAT=0x10100000L
.endif

.ifdef WITH_OPENSSL
CPPFLAGS+=-DWITH_OPENSSL
.if !defined(__MINIX)
CPPFLAGS+=-DENABLE_PKCS11
.endif
.endif

CPPFLAGS+=-DWITH_ZLIB

.if !defined(NOPIC) && !defined(__MINIX)
CPPFLAGS+=-DHAVE_DLOPEN
.endif
.PATH: ${SSHDIST}

.if (${USE_PAM} != "no")
CPPFLAGS+=-DUSE_PAM
.else	# USE_PAM == no
.if (${USE_SKEY} != "no")
CPPFLAGS+=-DSKEY
.endif
.endif	# USE_PAM == no

.if (${USE_KERBEROS} != "no")
CPPFLAGS+=-DGSSAPI
CPPFLAGS+=-DKRB5 -DHEIMDAL
.endif

.if (${USE_LDAP} != "no")
CPPFLAGS+=-DWITH_LDAP_PUBKEY
.endif

CPPFLAGS+=-DX11BASE=\"/usr/X11R7\"

.if defined(__MINIX)
# MEECHO: utmpx есть, utmp нет; libwrap в дереве нет вовсе.
CPPFLAGS+=-DSUPPORT_UTMPX
.else
CPPFLAGS+=-DSUPPORT_UTMP -DSUPPORT_UTMPX
CPPFLAGS+=-DLIBWRAP
.endif

LINTFLAGS+=	-X 117	# nonportable '>>' on signed integer type
LINTFLAGS+=	-X 231	# parameter unused
LINTFLAGS+=	-X 247	# pointer cast to unrelated type
LINTFLAGS+=	-X 351	# missing header declaration

# We install libssh in /usr/lib/private so applications don't
# accidentally link against it.  Set LIBSUBDIR so libssh.so.N and
# libssh.so.N.M will be installed there, and set PROGDPSUBDIRS so
# ssh/sshd/sshd-session/&c. will find it at link-time.
.if !defined(__MINIX)
LIBSUBDIR=	private
PROGDPSUBDIRS+=	private
.endif
