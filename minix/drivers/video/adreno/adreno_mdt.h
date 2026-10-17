/*
 * A Qualcomm "MDT" firmware image, read (adreno_mdt.c): the zap shader's
 * a512_zap.mdt and its a512_zap.bNN.  Plain C, nothing of MINIX in it, so
 * that the host stand reads the same files with the same code.
 */
#ifndef _ADRENO_MDT_H
#define _ADRENO_MDT_H

#include <stddef.h>
#include <stdint.h>

/* The segment type in p_flags 26:24, and the relocatable bit (Linux's
 * include/linux/soc/qcom/mdt_loader.h). */
#define MDT_TYPE(flags)		(((flags) >> 24) & 7)
#define MDT_TYPE_HASH		2
#define MDT_RELOCATABLE		(1u << 27)

#define MDT_MAXPH		16

struct mdt_ph {
	uint32_t type, offset, paddr, filesz, memsz, flags;
};

struct mdt {
	unsigned nph;
	struct mdt_ph ph[MDT_MAXPH];

	/* The metadata PAS_INIT_IMAGE takes: the first ehdr_size bytes of
	 * the file (program header 0's size: the ELF and program headers),
	 * then the hash segment, with nothing between. */
	unsigned hash;			/* the hash segment's number */
	uint32_t ehdr_size, hash_size;
	int hash_in_file;		/* in the .mdt, at hash_off */
	uint32_t hash_off;		/* else in its own .bNN */

	/* The loadable segments: PT_LOAD, not the hash, memsz not 0. */
	unsigned load[MDT_MAXPH];
	unsigned nload;
	int relocatable;		/* any of them RELOCATABLE */
	int split;			/* a segment ends past the .mdt: every
					 * loadable one comes from its .bNN */
	uint32_t min_addr, max_addr;	/* max: the end rounded up to 4 KiB */
};

/* 0, or a reason the file is not one this reads (*why). */
int mdt_parse(const void *file, size_t len, struct mdt *m, const char **why);

/* The name of segment n's file: "a512_zap.mdt" -> "a512_zap.b02".  0, or
 * -1 when the name is too short or the buffer too small. */
int mdt_seg_name(const char *mdt_name, unsigned n, char *out, size_t size);

#endif /* _ADRENO_MDT_H */
