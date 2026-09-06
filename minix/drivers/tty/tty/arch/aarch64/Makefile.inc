# Makefile for arch-dependent TTY code
.include <bsd.own.mk>

HERE=${.CURDIR}/arch/${MACHINE_ARCH}
.PATH:  ${HERE}

# rs232.c is the line tty sees; pl011.c and ns8250.c are the two parts it
# can find at the other end of it.  See uart.h.
SRCS +=	console.c keyboard.c rs232.c pl011.c ns8250.c
