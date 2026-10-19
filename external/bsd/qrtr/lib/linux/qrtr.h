/*
 * MEECHO: what libqrtr (and the programs on it) include as <linux/qrtr.h>.
 * The family's own header is <netqrtr/qrtr.h>, with Linux's names and
 * values; Linux's little-endian integer types, which libqrtr's ns.h uses,
 * are plain integers here - the machine is little-endian.
 */
#ifndef _LIBQRTR_LINUX_QRTR_H
#define _LIBQRTR_LINUX_QRTR_H

#include <stdint.h>
#include <netqrtr/qrtr.h>

typedef uint16_t __le16;
typedef uint32_t __le32;
typedef uint64_t __le64;

#endif /* _LIBQRTR_LINUX_QRTR_H */
