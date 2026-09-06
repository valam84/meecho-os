/*
 * fsck for MFS V4.
 *
 * The journal closes the case a check used to be run for - the machine
 * losing power in the middle of an operation.  What it does not close is a
 * mistake in the server, damage to the medium, or the blocks an operation
 * too large for one transaction leaks when it is cut in half.  So this
 * still exists; it is just no longer what every boot after a crash has to
 * wait for.
 *
 * The checks are in six passes, each standing on the one before it:
 *
 *	0. the superblock, and the journal, which is replayed if it holds
 *	   anything - checking a file system whose last operation is still
 *	   in the journal would mean repairing what is not broken;
 *	1. the inodes, and with them the block map of each: what blocks are
 *	   in use, which are claimed twice, which lie outside the device;
 *	2. the tree, from the root: the record chain of every directory
 *	   block, and where each entry points;
 *	3. link counts, against what the tree actually referred to;
 *	4. inodes in use that the tree never mentioned;
 *	5. the bitmaps on the disk, against the ones this program built.
 *
 * The design, and what was decided against, is in port/PORTING-LOG.md,
 * "Этап 7.5".
 */

#include <sys/types.h>
#include <sys/stat.h>

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <minix/journal.h>

#include "mfs/ondisk.h"
#include "exitvalues.h"
#include "fsck4.h"

/* Values of d_type, from <sys/dirent.h>, without dragging it in. */
#define DT_UNKNOWN	0
#define DT_FIFO		1
#define DT_CHR		2
#define DT_DIR		4
#define DT_BLK		6
#define DT_REG		8
#define DT_LNK		10
#define DT_SOCK		12

#define MAX_DEPTH	64	/* how deep a tree this will walk */
#define MAX_ROUNDS	4	/* passes over the whole file system */

static int fd;
static const char *fs_name;	/* the device, as the caller named it */
static int f_repair, f_auto, f_preen, f_listing;

static int changed;		/* the file system was written to */
static int unresolved;		/* something was wrong and stayed wrong */
static int nerrors;

static struct mfs4_super sb;
static size_t bsize;
static uint32_t ninodes, nblocks, firstdata, isize, ipb;
static uint32_t nindirs;	/* block numbers per indirect block */
static uint64_t imap_blk, zmap_blk, itab_blk;
static uint32_t zoff;		/* zone bitmap bit 0 is block zoff */

/* What this program worked out for itself, to be compared with the disk. */
static uint32_t *blk_used;	/* one bit per block of the device */
static uint32_t *ino_used;	/* one bit per inode */
static uint8_t *ino_type;	/* DT_* of each inode, 0 if free */
static uint32_t *ino_links;	/* links the tree turned out to have */
static uint8_t *ino_seen;	/* directories already walked into */

static uint32_t nregular, ndirectory, nsymlink, nspecial, nother;
static uint64_t nblocks_used;

/*===========================================================================*
 *				saying things				     *
 *===========================================================================*/
static void banner(void)
{
	static int done = 0;

	if (!done && f_preen) {
		printf("%s: ", fs_name);
		done = 1;
	}
}

static void problem(const char *fmt, ...)
{
	va_list ap;

	banner();
	nerrors++;

	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	printf("\n");
}

static void note(const char *fmt, ...)
{
	va_list ap;

	if (f_preen)
		return;

	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	printf("\n");
}

/*
 * Ask whether to repair something.  Without the right to write, the answer
 * is no and the file system is left as it was found; with -a it is yes and
 * nothing is asked.
 */
static int ask(const char *what)
{
	int c, answer;

	if (!f_repair) {
		printf("  (%s: NOT FIXED)\n", what);
		unresolved = 1;
		return 0;
	}

	if (f_auto) {
		printf("  (%s: FIXED)\n", what);
		return 1;
	}

	printf("  %s? [yn] ", what);
	fflush(stdout);

	answer = 0;
	while ((c = getchar()) != EOF && c != '\n')
		if (answer == 0)
			answer = c;

	if (answer == 'y' || answer == 'Y')
		return 1;

	unresolved = 1;
	return 0;
}

static void fatal(const char *fmt, ...)
{
	va_list ap;

	printf("%s: ", fs_name);

	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	printf("\n");

	exit(FSCK_EXIT_CHECK_FAILED);
}

static void *xalloc(size_t n);

/*===========================================================================*
 *				the device				     *
 *===========================================================================*/
/*
 * One block of cache, which is what turns this program from slow into fast:
 * an inode table of thirty thousand inodes is a thousand blocks, and read
 * without it that is thirty thousand trips to the driver instead.  The same
 * one block serves the directory walk, which reads a block per entry.
 */
static uint64_t cached_blk = (uint64_t) -1;
static char *cached_buf;

static int read_block(uint64_t blk, void *buf)
{
	ssize_t r;

	if (blk == cached_blk) {
		memcpy(buf, cached_buf, bsize);
		return 1;
	}

	r = pread(fd, buf, bsize, (off_t) blk * bsize);
	if (r != (ssize_t) bsize) {
		problem("cannot read block %"PRIu64" (%zd)", blk, r);
		return 0;
	}

	if (cached_buf == NULL)
		cached_buf = xalloc(bsize);

	memcpy(cached_buf, buf, bsize);
	cached_blk = blk;

	return 1;
}

static int write_block(uint64_t blk, const void *buf)
{
	ssize_t r;

	if (!f_repair)
		fatal("internal error: a write with no right to write");

	r = pwrite(fd, buf, bsize, (off_t) blk * bsize);
	if (r != (ssize_t) bsize) {
		problem("cannot write block %"PRIu64" (%zd)", blk, r);
		return 0;
	}

	/* Keep the one block of cache truthful. */
	if (blk == cached_blk)
		memcpy(cached_buf, buf, bsize);

	changed = 1;

	return 1;
}

static void *xalloc(size_t n)
{
	void *p;

	if ((p = calloc(1, n)) == NULL)
		fatal("out of memory (%zu bytes)", n);

	return p;
}

/*===========================================================================*
 *				bits					     *
 *===========================================================================*/
static void bit_set(uint32_t *map, uint64_t n)
{
	map[n / 32] |= 1UL << (n % 32);
}

static void bit_clear(uint32_t *map, uint64_t n)
{
	map[n / 32] &= ~(1UL << (n % 32));
}

static int bit_get(const uint32_t *map, uint64_t n)
{
	return (map[n / 32] >> (n % 32)) & 1;
}

/* Blocks and inodes as this program sees them. */
static void mark_block(uint64_t blk)
{
	bit_set(blk_used, blk);
	nblocks_used++;
}

static int block_marked(uint64_t blk)
{
	return bit_get(blk_used, blk);
}

/*===========================================================================*
 *				inodes					     *
 *===========================================================================*/
static uint64_t inode_block(uint32_t ino)
{
	return itab_blk + (ino - 1) / ipb;
}

static int inode_read(uint32_t ino, struct mfs4_inode *ip)
{
	static char *buf = NULL;

	if (buf == NULL)
		buf = xalloc(bsize);

	if (!read_block(inode_block(ino), buf))
		return 0;

	memcpy(ip, buf + ((ino - 1) % ipb) * isize, sizeof(*ip));

	return 1;
}

static int inode_write(uint32_t ino, const struct mfs4_inode *ip)
{
	static char *buf = NULL;

	if (buf == NULL)
		buf = xalloc(bsize);

	if (!read_block(inode_block(ino), buf))
		return 0;

	memcpy(buf + ((ino - 1) % ipb) * isize, ip, sizeof(*ip));

	return write_block(inode_block(ino), buf);
}

static int is_fast_symlink(const struct mfs4_inode *ip)
{
	return S_ISLNK(ip->i_mode) && ip->i_size > 0 &&
	    ip->i_size <= MFS4_FAST_SYMLINK_MAX;
}

static unsigned int type_of(uint16_t mode)
{
	switch (mode & S_IFMT) {
	case S_IFREG:	return DT_REG;
	case S_IFDIR:	return DT_DIR;
	case S_IFLNK:	return DT_LNK;
	case S_IFCHR:	return DT_CHR;
	case S_IFBLK:	return DT_BLK;
	case S_IFIFO:	return DT_FIFO;
	case S_IFSOCK:	return DT_SOCK;
	default:	return DT_UNKNOWN;
	}
}

static const char *type_name(unsigned int type)
{
	switch (type) {
	case DT_REG:	return "file";
	case DT_DIR:	return "directory";
	case DT_LNK:	return "symbolic link";
	case DT_CHR:	return "character device";
	case DT_BLK:	return "block device";
	case DT_FIFO:	return "fifo";
	case DT_SOCK:	return "socket";
	default:	return "something unknown";
	}
}

/*===========================================================================*
 *				the journal				     *
 *===========================================================================*/
/*
 * Replay whatever the journal holds, with this program's own code: the one
 * in libminixfs talks to a block driver through bdev, and this is an
 * ordinary program with an open file.  What the two share is the format.
 *
 * The journal is the blocks of a reserved inode, and the file is contiguous
 * and never changed after mkfs made it - which is what allows its inode to
 * be read before the file system has been made whole.
 */
static uint32_t crc_table[256];
static int crc_ready;

static uint32_t crc32(uint32_t crc, const void *data, size_t len)
{
	const uint8_t *p = data;
	uint32_t c;
	int i, k;

	if (!crc_ready) {
		for (i = 0; i < 256; i++) {
			c = (uint32_t) i;
			for (k = 0; k < 8; k++)
				c = (c & 1) ? (0xedb88320UL ^ (c >> 1)) :
				    (c >> 1);
			crc_table[i] = c;
		}
		crc_ready = 1;
	}

	crc ^= 0xffffffffUL;
	while (len-- > 0)
		crc = crc_table[(crc ^ *p++) & 0xff] ^ (crc >> 8);

	return crc ^ 0xffffffffUL;
}

/* The first block of the journal, and how many there are. */
static int journal_extent(uint64_t *start, uint32_t *count)
{
	struct mfs4_inode ino;
	uint32_t i;
	uint64_t b;

	if (sb.s_journal_inum == 0 || sb.s_journal_inum > ninodes)
		return 0;

	if (!inode_read(sb.s_journal_inum, &ino))
		return 0;

	*count = (uint32_t) (ino.i_size / bsize);
	if (*count < JOURNAL_MIN_BLOCKS)
		return 0;

	/* It has to be one run, and it has to be within the direct and
	 * single indirect blocks or this simple reader cannot follow it.
	 */
	*start = ino.i_zone[0];

	for (i = 0; i < *count && i < MFS4_NR_DZONES; i++)
		if (ino.i_zone[i] != *start + i)
			return 0;

	if (*count > MFS4_NR_DZONES) {
		static char *buf = NULL;
		uint32_t *ind;

		if (buf == NULL)
			buf = xalloc(bsize);
		if (ino.i_zone[MFS4_NR_DZONES] == 0 ||
		    !read_block(ino.i_zone[MFS4_NR_DZONES], buf))
			return 0;

		ind = (uint32_t *) buf;
		for (i = MFS4_NR_DZONES; i < *count; i++) {
			if (i - MFS4_NR_DZONES >= nindirs)
				return 0;	/* needs a second level */
			b = ind[i - MFS4_NR_DZONES];
			if (b != *start + i)
				return 0;
		}
	}

	return 1;
}

static int journal_replay(void)
{
	struct journal_super js;
	struct journal_head *jh;
	char *scratch, *blk;
	uint64_t start, *home = NULL;
	uint32_t count, njblocks, sequence, crc;
	unsigned int i;
	int replayed = 0;

	if (!(sb.s_feature_compat & MFS4_COMPAT_HAS_JOURNAL))
		return 0;

	if (!journal_extent(&start, &njblocks)) {
		problem("the journal is not where the superblock says it is");
		return 0;
	}

	scratch = xalloc(bsize);
	blk = xalloc(bsize);

	if (!read_block(start, scratch))
		goto out;

	memcpy(&js, scratch, sizeof(js));

	if (js.js_magic != JOURNAL_MAGIC || js.js_version != JOURNAL_VERSION ||
	    js.js_block_size != bsize || js.js_nblocks != njblocks ||
	    memcmp(js.js_uuid, sb.s_uuid, sizeof(js.js_uuid)) != 0) {
		problem("the journal is damaged or belongs to another volume");
		goto out;
	}

	/* From here on every way out goes through 'clear', which writes the
	 * journal superblock back, so the sequence number has to be known
	 * before any of them can be taken.
	 */
	sequence = js.js_sequence;

	if (js.js_start == 0)
		goto out;			/* nothing in it */

	if (!f_repair) {
		problem("the journal has a transaction in it, which has to "
		    "be replayed before this file system can be checked");
		unresolved = 1;
		goto out;
	}

	if (js.js_start >= njblocks || !read_block(start + js.js_start,
	    scratch))
		goto clear;

	jh = (struct journal_head *) scratch;

	if (jh->jh_magic != JOURNAL_MAGIC ||
	    jh->jh_type != JOURNAL_DESCRIPTOR ||
	    jh->jh_sequence != sequence)
		goto clear;

	count = jh->jh_count;
	if (count == 0 || count > JOURNAL_MAX_BLOCKS(bsize) ||
	    js.js_start + count + 2 > njblocks)
		goto clear;

	home = xalloc(count * sizeof(*home));
	for (i = 0; i < count; i++)
		home[i] = jh->jh_block[i];

	crc = crc32(0, scratch, bsize);
	for (i = 0; i < count; i++) {
		if (!read_block(start + js.js_start + 1 + i, blk))
			goto clear;
		crc = crc32(crc, blk, bsize);
	}

	if (!read_block(start + js.js_start + 1 + count, scratch))
		goto clear;

	jh = (struct journal_head *) scratch;
	if (jh->jh_magic != JOURNAL_MAGIC || jh->jh_type != JOURNAL_COMMIT ||
	    jh->jh_sequence != sequence || jh->jh_count != count ||
	    jh->jh_checksum != crc) {
		note("the journal holds a transaction that was never "
		    "committed; it is discarded");
		goto clear;
	}

	for (i = 0; i < count; i++) {
		if (home[i] >= nblocks) {
			problem("the journal names block %"PRIu64", which is "
			    "not on this device", home[i]);
			goto clear;
		}
		if (!read_block(start + js.js_start + 1 + i, blk))
			goto clear;
		if (!write_block(home[i], blk))
			goto clear;
	}

	printf("%s: replayed journal transaction %u, %u block(s)\n", fs_name,
	    sequence, count);
	replayed = 1;

clear:
	/* Empty the journal, whether its transaction was replayed or thrown
	 * away: what is on the disk now is a file system either way, and a
	 * journal that still points at something would be replayed again.
	 */
	memset(scratch, 0, bsize);
	js.js_start = 0;
	js.js_sequence = sequence + 1;
	memcpy(scratch, &js, sizeof(js));
	(void) write_block(start, scratch);

out:
	if (home != NULL) free(home);
	free(blk);
	free(scratch);

	return replayed;
}

/*===========================================================================*
 *			pass 0: the superblock				     *
 *===========================================================================*/
static uint32_t bitmap_blocks(uint64_t bits)
{
	uint64_t per = (uint64_t) bsize * 8;

	return (uint32_t) ((bits + per - 1) / per);
}

int fsck4_is_v4(int devfd)
{
	uint32_t magic;

	if (pread(devfd, &magic, sizeof(magic), 1024) != sizeof(magic))
		return 0;

	return magic == MFS4_SUPER_MAGIC;
}

static void check_super(void)
{
	uint32_t n;

	if (pread(fd, &sb, sizeof(sb), 1024) != (ssize_t) sizeof(sb))
		fatal("cannot read the superblock");

	if (sb.s_magic != MFS4_SUPER_MAGIC)
		fatal("this is not a V4 file system");
	if (sb.s_disk_version != 0)
		fatal("V4 sub-version %u is not known to this program",
		    sb.s_disk_version);

	if (sb.s_feature_incompat & ~(uint32_t) MFS4_INCOMPAT_SUPP)
		fatal("this file system has features 0x%x that this program "
		    "does not know; checking it could only damage it",
		    sb.s_feature_incompat & ~(uint32_t) MFS4_INCOMPAT_SUPP);

	bsize = sb.s_block_size;
	if (bsize < MFS4_MIN_BLOCK_SIZE || bsize > MFS4_MAX_BLOCK_SIZE ||
	    (bsize & (bsize - 1)) != 0)
		fatal("block size %u makes no sense", (unsigned int) bsize);

	isize = sb.s_inode_size;
	if (isize < MFS4_INODE_SIZE || bsize % isize != 0)
		fatal("inode size %u makes no sense", isize);

	ninodes = sb.s_ninodes;
	nblocks = sb.s_nblocks;
	firstdata = sb.s_firstdatablock;

	if (ninodes < 1 || nblocks < 8)
		fatal("a file system of %u inodes and %u blocks is not one",
		    ninodes, nblocks);

	ipb = (uint32_t) (bsize / isize);
	nindirs = (uint32_t) (bsize / sizeof(uint32_t));

	imap_blk = 2;
	zmap_blk = imap_blk + sb.s_imap_blocks;
	itab_blk = zmap_blk + sb.s_zmap_blocks;
	zoff = firstdata - 1;

	n = bitmap_blocks((uint64_t) ninodes + 1);
	if (sb.s_imap_blocks != n)
		problem("the inode bitmap is %u blocks, and %u is what %u "
		    "inodes need", sb.s_imap_blocks, n, ninodes);

	n = bitmap_blocks(nblocks);
	if (sb.s_zmap_blocks != n)
		problem("the block bitmap is %u blocks, and %u is what %u "
		    "blocks need", sb.s_zmap_blocks, n, nblocks);

	n = (uint32_t) (itab_blk + (ninodes + ipb - 1) / ipb);
	if (firstdata != n)
		problem("the first data block is %u, and the maps and the "
		    "inode table end at %u", firstdata, n);

	if (firstdata >= nblocks)
		fatal("the file system has no room for any data");

	if (f_listing) {
		printf("V4 file system on %s\n", fs_name);
		printf("  %u inodes of %u bytes, %u blocks of %u bytes\n",
		    ninodes, isize, nblocks, (unsigned int) bsize);
		printf("  first data block %u, inode table at %"PRIu64"\n",
		    firstdata, itab_blk);
		printf("  features: compat 0x%x ro_compat 0x%x incompat 0x%x\n",
		    sb.s_feature_compat, sb.s_feature_ro_compat,
		    sb.s_feature_incompat);
		if (sb.s_feature_compat & MFS4_COMPAT_HAS_JOURNAL)
			printf("  journal: inode %u, %u blocks\n",
			    sb.s_journal_inum, sb.s_journal_blocks);
	}
}

static void write_super(void)
{
	if (!f_repair)
		return;

	if (pwrite(fd, &sb, sizeof(sb), 1024) != (ssize_t) sizeof(sb))
		problem("cannot write the superblock");
	else
		changed = 1;
}

/*===========================================================================*
 *			pass 1: inodes and their blocks			     *
 *===========================================================================*/
struct walk {
	uint32_t ino;
	uint64_t present;	/* blocks the file has */
	uint64_t highest;	/* highest file block index in use, plus one */
	int bad;
	int unmark;		/* give the blocks back instead of taking */
};

/*
 * Take note of one block of a file.  Everything that can be wrong with a
 * block number is wrong here: outside the device, inside the area the file
 * system keeps for itself, or already spoken for by somebody else.
 */
static int use_block(struct walk *w, uint64_t blk, const char *what)
{
	/* Walking an inode that is being freed: what it holds goes back to
	 * nobody, and the bitmap pass will see it as free.
	 */
	if (w->unmark) {
		if (blk < nblocks && block_marked(blk)) {
			bit_clear(blk_used, blk);
			nblocks_used--;
		}
		return 1;
	}

	if (blk >= nblocks) {
		problem("inode %u has %s %"PRIu64", which is not on this "
		    "device", w->ino, what, blk);
		w->bad = 1;
		return 0;
	}
	if (blk < firstdata) {
		problem("inode %u has %s %"PRIu64", which is inside the "
		    "file system's own area", w->ino, what, blk);
		w->bad = 1;
		return 0;
	}
	if (block_marked(blk)) {
		problem("block %"PRIu64" is claimed by inode %u and by "
		    "something else as well", blk, w->ino);
		w->bad = 1;
		return 0;
	}

	mark_block(blk);

	return 1;
}

/* Walk one subtree of indirect blocks, marking everything it names. */
static void walk_indirect(struct walk *w, uint64_t blk, unsigned int level,
	uint64_t first, uint64_t span)
{
	static char *buf[MFS4_NR_LEVELS];
	uint32_t *ind;
	unsigned int i;

	if (!use_block(w, blk, "an indirect block"))
		return;

	if (buf[level - 1] == NULL)
		buf[level - 1] = xalloc(bsize);

	if (!read_block(blk, buf[level - 1]))
		return;

	ind = (uint32_t *) buf[level - 1];
	span /= nindirs;

	for (i = 0; i < nindirs; i++) {
		uint64_t index = first + (uint64_t) i * span;

		if (ind[i] == 0)
			continue;

		if (level > 1) {
			walk_indirect(w, ind[i], level - 1, index, span);
			continue;
		}

		if (!use_block(w, ind[i], "a block"))
			continue;

		w->present++;
		if (index + 1 > w->highest)
			w->highest = index + 1;
	}
}

static void walk_blocks(struct walk *w, const struct mfs4_inode *ip)
{
	uint64_t first, span;
	unsigned int i, level;

	for (i = 0; i < MFS4_NR_DZONES; i++) {
		if (ip->i_zone[i] == 0)
			continue;
		if (!use_block(w, ip->i_zone[i], "a block"))
			continue;
		w->present++;
		if (i + 1 > w->highest)
			w->highest = i + 1;
	}

	first = MFS4_NR_DZONES;
	span = nindirs;

	for (level = 1; level <= MFS4_NR_LEVELS; level++) {
		if (ip->i_zone[MFS4_NR_DZONES + level - 1] != 0)
			walk_indirect(w, ip->i_zone[MFS4_NR_DZONES + level - 1],
			    level, first, span);

		first += span;
		span *= nindirs;
	}
}

static void clear_inode(uint32_t ino, struct mfs4_inode *ip, const char *why)
{
	problem("inode %u %s", ino, why);

	if (!ask("clear it"))
		return;

	memset(ip, 0, sizeof(*ip));
	(void) inode_write(ino, ip);

	ino_type[ino] = 0;
	bit_clear(ino_used, ino);
	/* Its blocks, whatever they were, are nobody's now; the bitmap pass
	 * will free them, because this program never marked them used.
	 */
}

static void pass1_inodes(void)
{
	struct mfs4_inode ino;
	struct walk w;
	uint64_t expect;
	uint32_t n;

	note("** Phase 1 - inodes and the blocks they claim");

	for (n = 1; n <= ninodes; n++) {
		if (!inode_read(n, &ino))
			continue;

		if (ino.i_mode == 0)
			continue;		/* free */

		ino_type[n] = (uint8_t) type_of(ino.i_mode);
		bit_set(ino_used, n);

		if (ino_type[n] == DT_UNKNOWN) {
			clear_inode(n, &ino, "is of no known type");
			continue;
		}

		switch (ino_type[n]) {
		case DT_REG:	nregular++;	break;
		case DT_DIR:	ndirectory++;	break;
		case DT_LNK:	nsymlink++;	break;
		case DT_CHR:
		case DT_BLK:	nspecial++;	break;
		default:	nother++;	break;
		}

		if (ino.i_nlinks == 0) {
			clear_inode(n, &ino, "is in use but has no links");
			continue;
		}

		/* A device is a device number, not blocks; a short symbolic
		 * link is its own target.  Neither has a block map, and
		 * walking one would claim blocks at random.
		 */
		if (ino_type[n] == DT_CHR || ino_type[n] == DT_BLK)
			continue;
		if (is_fast_symlink(&ino))
			continue;

		if (ino.i_size > sb.s_max_size) {
			clear_inode(n, &ino, "is larger than this file system "
			    "can address");
			continue;
		}

		memset(&w, 0, sizeof(w));
		w.ino = n;
		walk_blocks(&w, &ino);

		expect = (ino.i_size + bsize - 1) / bsize;

		if (w.highest > expect)
			problem("inode %u has blocks past its size of "
			    "%"PRIu64" bytes", n, (uint64_t) ino.i_size);

		/*
		 * A block that belongs to two files, or to no part of the
		 * device at all, cannot be sorted out by keeping both: there
		 * is no way to tell which of the two claims is the true one.
		 * The one found second goes, and the blocks it had taken
		 * before it stay marked used until the next run of this
		 * program gives them back - space lost, not damage done.
		 */
		if (w.bad) {
			clear_inode(n, &ino, "claims blocks that are not its "
			    "own to claim");
			continue;
		}

		if (ino_type[n] == DT_DIR) {
			if (ino.i_size % bsize != 0)
				problem("directory %u is %"PRIu64" bytes, "
				    "which is not a whole number of blocks",
				    n, (uint64_t) ino.i_size);
			if (w.present != expect || w.highest != expect)
				problem("directory %u has %"PRIu64" of the "
				    "%"PRIu64" blocks its size calls for",
				    n, w.present, expect);
		}
	}
}

/*===========================================================================*
 *			pass 2: the tree				     *
 *===========================================================================*/
/*
 * The block of a file at the given index, or 0 if there is none.  This
 * walks the map for one block at a time, which is what a directory - a few
 * blocks at most - deserves.
 */
static uint64_t bmap(const struct mfs4_inode *ip, uint64_t index)
{
	static char *buf = NULL;
	uint32_t *ind;
	uint64_t span, blk;
	unsigned int level, slot;

	if (index < MFS4_NR_DZONES)
		return ip->i_zone[index];

	index -= MFS4_NR_DZONES;
	span = 1;

	for (level = 1; level <= MFS4_NR_LEVELS; level++) {
		span *= nindirs;
		if (index < span)
			break;
		index -= span;
	}
	if (level > MFS4_NR_LEVELS)
		return 0;

	slot = MFS4_NR_DZONES + level - 1;
	blk = ip->i_zone[slot];

	if (buf == NULL)
		buf = xalloc(bsize);

	while (blk != 0 && level > 0) {
		span /= nindirs;
		if (!read_block(blk, buf))
			return 0;
		ind = (uint32_t *) buf;
		blk = ind[span ? index / span : index];
		if (span)
			index %= span;
		level--;
	}

	return blk;
}

static void name_of(const struct mfs4_dirent *dp, char *out, size_t outsize)
{
	size_t i, n = dp->d_name_len;

	if (n > outsize - 1)
		n = outsize - 1;

	for (i = 0; i < n; i++)
		out[i] = isprint((unsigned char) dp->d_name[i]) ?
		    dp->d_name[i] : '?';

	out[n] = '\0';
}

/*
 * Take an entry out of a directory block: give its room to the record
 * before it, or, if it is the first of the block, mark it free.  This is
 * the rule the server follows, and the two have to agree on it.
 */
static void remove_entry(struct mfs4_dirent *prev, struct mfs4_dirent *dp)
{
	if (prev != NULL)
		prev->d_rec_len += dp->d_rec_len;
	else
		dp->d_ino = 0;
}

static void descend(uint32_t ino, uint32_t parent, int depth);

/* One block of a directory: walk its records and check where they point. */
static void check_dir_block(uint32_t ino, uint32_t parent, uint64_t blk,
	uint64_t index, int depth, int *seen_dot, int *seen_dotdot)
{
	char *buf, name[MFS4_NAME_MAX + 1];
	struct mfs4_dirent *dp, *prev;
	unsigned int off;
	int dirty = 0;

	buf = xalloc(bsize);
	if (!read_block(blk, buf)) {
		free(buf);
		return;
	}

	prev = NULL;

	for (off = 0; off + MFS4_DIRENT_HDR <= bsize; off += dp->d_rec_len) {
		uint32_t child;

		dp = (struct mfs4_dirent *) (buf + off);

		if (dp->d_rec_len < MFS4_DIRENT_HDR ||
		    dp->d_rec_len % MFS4_DIRENT_ALIGN != 0 ||
		    off + dp->d_rec_len > bsize) {
			problem("directory %u has a record at %"PRIu64"+%u "
			    "whose length %u leads nowhere", ino, index * bsize,
			    off, dp->d_rec_len);
			/* The chain cannot be followed any further, and
			 * guessing where the next record starts would be
			 * guessing.  Give the rest of the block to this
			 * record, which loses the entries after it.
			 */
			if (ask("give the rest of the block to this record")) {
				dp->d_ino = 0;
				dp->d_rec_len = (uint16_t) (bsize - off);
				dirty = 1;
			}
			break;
		}

		if (dp->d_ino == 0) {
			prev = dp;
			continue;
		}

		if (dp->d_name_len == 0 ||
		    MFS4_DIRENT_HDR + dp->d_name_len > dp->d_rec_len) {
			problem("directory %u has an entry with a name of "
			    "%u bytes that does not fit in it", ino,
			    dp->d_name_len);
			if (ask("remove the entry")) {
				remove_entry(prev, dp);
				dirty = 1;
			}
			prev = dp;
			continue;
		}

		name_of(dp, name, sizeof(name));
		child = dp->d_ino;

		if (child == 0 || child > ninodes) {
			problem("directory %u has \"%s\" pointing at inode "
			    "%u, which does not exist", ino, name, child);
			if (ask("remove the entry")) {
				remove_entry(prev, dp);
				dirty = 1;
			}
			prev = dp;
			continue;
		}

		if (ino_type[child] == 0) {
			problem("directory %u has \"%s\" pointing at inode "
			    "%u, which is not in use", ino, name, child);
			if (ask("remove the entry")) {
				remove_entry(prev, dp);
				dirty = 1;
			}
			prev = dp;
			continue;
		}

		/* "." and ".." are the two the file system itself relies on. */
		if (!strcmp(name, ".")) {
			if (child != ino) {
				problem("directory %u has \".\" pointing at "
				    "inode %u", ino, child);
				if (ask("point it at itself")) {
					dp->d_ino = ino;
					child = ino;
					dirty = 1;
				}
			}
			*seen_dot = 1;
		} else if (!strcmp(name, "..")) {
			if (child != parent) {
				problem("directory %u has \"..\" pointing at "
				    "inode %u, not at %u", ino, child, parent);
				if (ask("point it at the parent")) {
					dp->d_ino = parent;
					child = parent;
					dirty = 1;
				}
			}
			*seen_dotdot = 1;
		}

		if (dp->d_type != DT_UNKNOWN &&
		    dp->d_type != ino_type[child]) {
			problem("directory %u calls \"%s\" a %s, and inode %u "
			    "is a %s", ino, name, type_name(dp->d_type), child,
			    type_name(ino_type[child]));
			if (ask("correct the type in the entry")) {
				dp->d_type = ino_type[child];
				dirty = 1;
			}
		}

		ino_links[child]++;

		/* Down into subdirectories, but not into "." and "..", and
		 * not twice into the same one.
		 */
		if (ino_type[child] == DT_DIR && strcmp(name, ".") &&
		    strcmp(name, "..")) {
			if (ino_seen[child]) {
				problem("directory %u is reached by more than "
				    "one path; \"%s\" in %u is the second",
				    child, name, ino);
				if (ask("remove the entry")) {
					remove_entry(prev, dp);
					ino_links[child]--;
					dirty = 1;
				}
			} else {
				descend(child, ino, depth + 1);
			}
		}

		prev = dp;
	}

	if (dirty)
		(void) write_block(blk, buf);

	free(buf);
}

static void descend(uint32_t ino, uint32_t parent, int depth)
{
	struct mfs4_inode dir;
	uint64_t index, nblk, blk;
	int seen_dot = 0, seen_dotdot = 0;

	if (depth > MAX_DEPTH) {
		problem("the tree is deeper than %d directories at inode %u; "
		    "not going further", MAX_DEPTH, ino);
		return;
	}

	ino_seen[ino] = 1;

	if (!inode_read(ino, &dir))
		return;

	nblk = dir.i_size / bsize;

	for (index = 0; index < nblk; index++) {
		if ((blk = bmap(&dir, index)) == 0) {
			problem("directory %u has a hole where its block "
			    "%"PRIu64" should be", ino, index);
			continue;
		}
		if (blk >= nblocks)
			continue;	/* pass 1 said so already */

		check_dir_block(ino, parent, blk, index, depth, &seen_dot,
		    &seen_dotdot);
	}

	if (!seen_dot)
		problem("directory %u has no \".\"", ino);
	if (!seen_dotdot)
		problem("directory %u has no \"..\"", ino);
}

static void pass2_tree(void)
{
	struct mfs4_inode root;

	note("** Phase 2 - the tree, from the root");

	if (!inode_read(1, &root))
		fatal("cannot read the root inode");

	if (!S_ISDIR(root.i_mode))
		fatal("the root inode is not a directory; this file system "
		    "cannot be repaired by this program");

	/* The root is its own parent: its ".." is the reference that any
	 * other directory gets from the one above it, and its "." is the
	 * second.  So it is counted like every other directory, and nothing
	 * is added here.
	 */
	descend(1, 1, 0);
}

/*===========================================================================*
 *			pass 3: link counts				     *
 *===========================================================================*/
static void pass3_links(void)
{
	struct mfs4_inode ino;
	uint32_t n;

	note("** Phase 3 - link counts");

	for (n = 1; n <= ninodes; n++) {
		if (ino_type[n] == 0 || ino_links[n] == 0)
			continue;

		if (!inode_read(n, &ino))
			continue;

		if (ino.i_nlinks == ino_links[n])
			continue;

		problem("inode %u says it has %u link(s), and the tree has "
		    "%u", n, ino.i_nlinks, ino_links[n]);

		if (ask("correct the count")) {
			ino.i_nlinks = ino_links[n];
			(void) inode_write(n, &ino);
		}
	}
}

/*===========================================================================*
 *			pass 4: inodes nothing points at			     *
 *===========================================================================*/
static void pass4_orphans(void)
{
	struct mfs4_inode ino;
	uint32_t n;

	note("** Phase 4 - inodes the tree does not mention");

	for (n = 2; n <= ninodes; n++) {
		if (ino_type[n] == 0 || ino_links[n] != 0)
			continue;

		/* The journal has no name and is meant not to: the
		 * superblock is what finds it, the way ext3 finds its own.
		 */
		if (n == sb.s_journal_inum)
			continue;

		if (!inode_read(n, &ino))
			continue;

		problem("inode %u is a %s in use that no directory mentions",
		    n, type_name(ino_type[n]));

		if (ask("free it")) {
			struct walk w;

			/* Its blocks were taken in pass 1; give them back
			 * before the inode that named them is gone.
			 */
			memset(&w, 0, sizeof(w));
			w.ino = n;
			w.unmark = 1;
			if (ino_type[n] != DT_CHR && ino_type[n] != DT_BLK &&
			    !is_fast_symlink(&ino))
				walk_blocks(&w, &ino);

			memset(&ino, 0, sizeof(ino));
			(void) inode_write(n, &ino);
			ino_type[n] = 0;
			bit_clear(ino_used, n);
		}
	}
}

/*===========================================================================*
 *			pass 5: the bitmaps				     *
 *===========================================================================*/
/*
 * Compare a bitmap on the disk with the one this program built, and write
 * ours over it if they differ.  A bitmap is a summary of what the inodes
 * say, so where they disagree the inodes are right by construction.
 */
static void check_bitmap(const char *what, uint64_t first_blk,
	uint32_t nmapblocks, const uint32_t *ours, uint64_t nbits,
	uint64_t bit_offset)
{
	uint32_t *disk;
	uint64_t i, wrong = 0;
	size_t mapbytes;
	uint32_t b;

	mapbytes = (size_t) nmapblocks * bsize;
	disk = xalloc(mapbytes);

	for (b = 0; b < nmapblocks; b++) {
		if (!read_block(first_blk + b, (char *) disk + b * bsize)) {
			free(disk);
			return;
		}
	}

	for (i = 0; i < nbits; i++) {
		int mine = (i == 0) ? 1 : bit_get(ours, i + bit_offset);
		int theirs = bit_get(disk, i);

		if (mine != theirs)
			wrong++;
	}

	if (wrong == 0) {
		free(disk);
		return;
	}

	problem("the %s bitmap disagrees with the inodes in %"PRIu64" place(s)",
	    what, wrong);

	if (ask("rebuild it")) {
		memset(disk, 0, mapbytes);
		for (i = 0; i < nbits; i++)
			if (i == 0 || bit_get(ours, i + bit_offset))
				bit_set(disk, i);

		for (b = 0; b < nmapblocks; b++)
			(void) write_block(first_blk + b,
			    (char *) disk + b * bsize);
	}

	free(disk);
}

static void pass5_bitmaps(void)
{
	note("** Phase 5 - the bitmaps");

	/* Inode bitmap: bit i is inode i, and bit 0 is always set. */
	check_bitmap("inode", imap_blk, sb.s_imap_blocks, ino_used,
	    (uint64_t) ninodes + 1, 0);

	/* Block bitmap: bit k is block zoff + k, and bit 0 is always set. */
	check_bitmap("block", zmap_blk, sb.s_zmap_blocks, blk_used,
	    (uint64_t) nblocks - zoff, zoff);
}

/*===========================================================================*
 *				fsck4_check				     *
 *===========================================================================*/
static void report(void)
{
	if (f_preen && nerrors == 0)
		return;

	printf("%s%u file%s, %u director%s, %u symbolic link%s, "
	    "%u device%s\n", f_preen ? "" : "\n",
	    nregular, nregular == 1 ? "" : "s",
	    ndirectory, ndirectory == 1 ? "y" : "ies",
	    nsymlink, nsymlink == 1 ? "" : "s",
	    nspecial, nspecial == 1 ? "" : "s");
	printf("%"PRIu64" of %u blocks in use\n", nblocks_used,
	    nblocks - firstdata);
}

int fsck4_check(int devfd, const char *name, int flags)
{
	size_t nb_blk, nb_ino;
	int round;

	fd = devfd;
	fs_name = name;
	f_repair = (flags & FSCK4_REPAIR) != 0;
	f_auto = (flags & FSCK4_AUTO) != 0;
	f_preen = (flags & FSCK4_PREEN) != 0;
	f_listing = (flags & FSCK4_LISTING) != 0;

	check_super();

	/* Preening skips a file system that was put away cleanly, which
	 * with a journal is nearly always.
	 */
	if (f_preen && (sb.s_flags & MFS4_FLAG_CLEAN) &&
	    !(sb.s_feature_incompat & MFS4_INCOMPAT_RECOVER)) {
		printf("%s: clean\n", fs_name);
		return FSCK_EXIT_OK;
	}

	if (f_preen)
		printf("%s: dirty, checking\n", fs_name);

	(void) journal_replay();

	nb_blk = ((size_t) nblocks + 31) / 32 * sizeof(uint32_t);
	nb_ino = ((size_t) ninodes + 32) / 32 * sizeof(uint32_t);

	blk_used = xalloc(nb_blk);
	ino_used = xalloc(nb_ino);
	ino_type = xalloc((size_t) ninodes + 1);
	ino_links = xalloc(((size_t) ninodes + 1) * sizeof(*ino_links));
	ino_seen = xalloc((size_t) ninodes + 1);

	/*
	 * One repair can uncover the next: an inode cleared in pass 1 leaves
	 * the directory entry that named it pointing at nothing, and that is
	 * only seen in pass 2 of the round after.  So the passes run again
	 * until a round changes nothing, which is what lets a boot script
	 * run this once and get a file system it can mount.
	 */
	for (round = 1; round <= MAX_ROUNDS; round++) {
		int changed_before = changed;

		unresolved = 0;
		nerrors = 0;
		nregular = ndirectory = nsymlink = nspecial = nother = 0;
		nblocks_used = 0;

		memset(blk_used, 0, nb_blk);
		memset(ino_used, 0, nb_ino);
		memset(ino_type, 0, (size_t) ninodes + 1);
		memset(ino_links, 0,
		    ((size_t) ninodes + 1) * sizeof(*ino_links));
		memset(ino_seen, 0, (size_t) ninodes + 1);

		if (round > 1)
			note("** Again, to see what the repairs uncovered");

		pass1_inodes();
		pass2_tree();
		pass3_links();
		pass4_orphans();
		pass5_bitmaps();

		if (!f_repair || changed == changed_before)
			break;
	}

	if (round > MAX_ROUNDS) {
		problem("still finding things to repair after %d rounds; "
		    "run this again", MAX_ROUNDS);
		unresolved = 1;
	}

	report();

	/*
	 * A file system that was checked and repaired is clean, and one that
	 * was replayed does not need recovering any more.  A file system
	 * something is still wrong with is left marked dirty, so that the
	 * next mount does not trust it either.
	 */
	if (f_repair && !unresolved) {
		if (!(sb.s_flags & MFS4_FLAG_CLEAN) ||
		    (sb.s_feature_incompat & MFS4_INCOMPAT_RECOVER)) {
			sb.s_flags |= MFS4_FLAG_CLEAN;
			sb.s_feature_incompat &=
			    ~(uint32_t) MFS4_INCOMPAT_RECOVER;
			write_super();
			if (!f_preen)
				printf("\n----- FILE SYSTEM MARKED CLEAN "
				    "-----\n\n");
		}
	}

	if (changed && !f_preen)
		printf("----- FILE SYSTEM WAS MODIFIED -----\n\n");

	free(blk_used);
	free(ino_used);
	free(ino_type);
	free(ino_links);
	free(ino_seen);

	if (unresolved)
		return FSCK_EXIT_UNRESOLVED;

	return FSCK_EXIT_OK;
}
