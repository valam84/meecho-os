#	$NetBSD: Makefile,v 1.12 2013/02/12 20:31:14 christos Exp $

.include <bsd.own.mk>

# MEECHO: openssh был исключён из сборки MINIX вместе с самим импортом -
# каталога dist в дереве не было вовсе. Импортирован заново из
# NetBSD-current (OpenSSH 10.3) и собирается без libcrypto; почему именно
# так - в openssh/Makefile.inc и в PORTING-LOG.md, «Этап 8.4: ssh».
SUBDIR+= openssl .WAIT heimdal netpgp .WAIT openssh libsaslc

.include <bsd.subdir.mk>
