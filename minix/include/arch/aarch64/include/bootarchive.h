#ifndef _AARCH64_BOOTARCHIVE_H
#define _AARCH64_BOOTARCHIVE_H

/*
 * The boot archive: how the boot images reach the kernel on AArch64.
 *
 * A device tree can name exactly one extra range - /chosen/linux,initrd-start
 * and -end - and MINIX needs a dozen separate ELF images. So the loader is
 * handed one blob and the kernel takes it apart, which means a format, and
 * this is it.
 *
 *
 * Why not cpio, which every system already has tools for
 * ------------------------------------------------------
 * Because of where the parsing happens. pre_init() reads this before the MMU
 * is on, with no allocator, no libc worth the name, and no exception vectors
 * for the first part of it - the least forgiving code in the system. A cpio
 * newc header is 110 bytes of ASCII hex followed by a variable-length name
 * with its own padding rules; reading it means parsing text in exactly the
 * place where a mistake is hardest to see.
 *
 * This format is read with four loads and a bounds check. Both ends of it are
 * ours - the kernel here and mkbootarchive(1) in
 * minix/kernel/arch/aarch64/tools - so nothing is gained by a format a third
 * party might also write, and the tool can print its own table of contents
 * for the times when a human wants to look.
 *
 *
 * Rules the layout follows, and why
 * ---------------------------------
 * Every field is naturally aligned. Until the MMU is on, every access is to
 * Device memory, where an unaligned load faults regardless of SCTLR_EL1.A -
 * this port already lost a boot to that, through a packed multiboot
 * structure. Nothing here is packed and nothing needs to be.
 *
 * Numbers are little-endian, which is the byte order of the machine that
 * writes them and of the machine that reads them. Unlike the device tree,
 * this blob is not an interchange format: it is built by our own tool for our
 * own kernel, in the same build. A big-endian target would have to byte-swap
 * here, and would notice, because the magic would not match.
 *
 * Images start on a page boundary. The kernel cuts their range out of free
 * memory a page at a time, and libexec reads an ELF header straight out of
 * the blob.
 */

#include <stdint.h>

/* The first four bytes of the blob read as "MBA1" - MINIX boot archive,
 * version 1 - in a hex dump, which is how a human identifies one. */
#define BOOT_ARCHIVE_MAGIC	0x3141424dU	/* 'M','B','A','1' LE */

#define BOOT_ARCHIVE_VERSION	1

/* Names are fixed width and NUL-padded; a name may fill the field. */
#define BOOT_ARCHIVE_NAMELEN	32

/* Images begin on a page boundary, and so does the first one. */
#define BOOT_ARCHIVE_ALIGN	4096

struct boot_archive_entry {
	uint64_t offset;	/* from the start of the archive */
	uint64_t size;		/* bytes of image */
	char name[BOOT_ARCHIVE_NAMELEN];
};

struct boot_archive_header {
	uint32_t magic;		/* BOOT_ARCHIVE_MAGIC */
	uint32_t version;	/* BOOT_ARCHIVE_VERSION */
	uint32_t count;		/* number of entries that follow */
	uint32_t reserved;	/* zero; keeps the entries 8-byte aligned */
	uint64_t total_size;	/* whole archive, header included */
	struct boot_archive_entry entry[];
};

#endif /* _AARCH64_BOOTARCHIVE_H */
