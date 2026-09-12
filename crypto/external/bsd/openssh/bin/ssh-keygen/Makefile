#	$NetBSD: Makefile,v 1.12 2025/12/28 09:39:35 nia Exp $

BINDIR=	/usr/bin

PROG=	ssh-keygen
SRCS=	ssh-keygen.c sshsig.c

.include "../Makefile.inc"

.ifdef WITH_OPENSSL
SRCS+=	moduli.c
.endif

COPTS.ssh-keygen.c+=	-Wno-pointer-sign

COPTS.ssh-keygen.c+=	${CC_WNO_FORMAT_TRUNCATION}
COPTS.ssh-keygen.c+= -Wno-error=deprecated-declarations
COPTS.moduli.c+= -Wno-error=deprecated-declarations
COPTS.ssh-keygen.c+= ${${ACTIVE_CC} == "clang":? -Wno-error=invalid-noreturn :}
# MEECHO: GCC 15 says the same thing about do_convert_to_pkcs8() and
# friends - __dead functions whose switch falls off the end - and has no
# flag for that diagnostic short of -Wno-error. Imported code; the file
# keeps its warnings, they just stop being fatal.
COPTS.ssh-keygen.c+= ${${ACTIVE_CC} == "gcc":? -Wno-error :}

.include <bsd.prog.mk>
