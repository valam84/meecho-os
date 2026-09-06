#ifndef __MFS_TYPE_H__
#define __MFS_TYPE_H__

#include <minix/libminixfs.h>

#include "ondisk.h"

/* The disk inode of a V3 file system, under the name the code has always
 * used for it.  The layouts themselves, of both versions, are in ondisk.h.
 */
typedef struct mfs3_inode d2_inode;

#endif

