#	$NetBSD: Makefile,v 1.4 2025/12/28 15:34:59 thorpej Exp $

.include <bsd.own.mk>

SUBDIR=	sshd-auth sshd-session

# MEECHO: MKFIDO2 в этом дереве нет вовсе (bsd.own.mk 2015 года), а
# libfido2 нет и подавно - ключи FIDO/U2F не собираются.
.if defined(MKFIDO2) && (${MKFIDO2} != "no")
SUBDIR+= ssh-sk-helper
.endif

.include <bsd.subdir.mk>
