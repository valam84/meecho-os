/*
 * Стенд mali: кодировка ioctl MINIX, а не Linux хоста (sys/sys/ioccom.h
 * дерева, ветка __minix): длина в 12 битах, направление в старших.
 * Номера запросов DRM стенд строит этими же макросами, как их строят
 * libdrm и Mesa на машине.
 */
#ifndef _SHIM_SYS_IOCCOM_H
#define _SHIM_SYS_IOCCOM_H
#define IOC_BIG		(unsigned long)0x10000000
#define IOCPARM_MASK	0xfff
#define IOCPARM_SHIFT	16
#define IOCGROUP_SHIFT	8
#define IOCPARM_LEN(x)	(((x) >> IOCPARM_SHIFT) & IOCPARM_MASK)
#define IOCBASECMD(x)	((x) & ~(IOCPARM_MASK << IOCPARM_SHIFT))
#define IOCGROUP(x)	(((x) >> IOCGROUP_SHIFT) & 0xff)
#define IOC_VOID	(unsigned long)0x20000000
#define IOC_OUT		(unsigned long)0x40000000
#define IOC_IN		(unsigned long)0x80000000
#define IOC_INOUT	(IOC_IN|IOC_OUT)
#define IOC_DIRMASK	(unsigned long)0xe0000000
#define _IOC(inout, group, num, len) \
    ((inout) | (((len) & IOCPARM_MASK) << IOCPARM_SHIFT) | \
    ((group) << IOCGROUP_SHIFT) | (num))
#define _IO(g,n)	_IOC(IOC_VOID,	(g), (n), 0)
#define _IOR(g,n,t)	_IOC(IOC_OUT,	(g), (n), sizeof(t))
#define _IOW(g,n,t)	_IOC(IOC_IN,	(g), (n), sizeof(t))
#define _IOWR(g,n,t)	_IOC(IOC_INOUT,	(g), (n), sizeof(t))
#endif
