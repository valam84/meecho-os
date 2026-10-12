/* Стенд: номера ioctl в раскладке MINIX - их нет на хосте Linux. */
#ifndef _STUB_SYS_IOCCOM_H
#define _STUB_SYS_IOCCOM_H
#define IOCPARM_MASK	0xfff
#define IOC_OUT		0x40000000UL
#define _IOC(inout, group, num, len) \
	((inout) | (((len) & IOCPARM_MASK) << 16) | ((group) << 8) | (num))
#define _IOR(g, n, t)	_IOC(IOC_OUT, (g), (n), sizeof(t))
#endif
