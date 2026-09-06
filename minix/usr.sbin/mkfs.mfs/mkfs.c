/* mkfs  -  make the MINIX filesystem	Authors: Tanenbaum et al. */

/*	Authors: Andy Tanenbaum, Paul Ogilvie, Frans Meulenbroeks, Bruce Evans */

#if HAVE_NBTOOL_CONFIG_H
#include "nbtool_config.h"
#endif

#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/stat.h>

#if defined(__minix)
#include <minix/minlib.h>
#include <minix/partition.h>
#include <sys/ioctl.h>
#endif

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Definition of the file system layout: */
#include "const.h"
#include "type.h"
#include "mfsdir.h"
#include "super.h"
/* The layouts of both formats, the same header the server reads.  The path
 * is relative to this file, so that it is found the same way whether this
 * is built for the system or as a host tool.
 */
#include "../../fs/mfs/ondisk.h"
/* The journal format, for the same reason and by the same kind of path. */
#include "../../include/minix/journal.h"

#define INODE_MAP	START_BLOCK

/* The largest file a V3 file system can hold; V4 keeps its own limit in
 * the superblock.  Stated here as well as in super(), which is where it
 * used to be alone, because the inode writer checks against it too.
 */
#ifndef MAX_MAX_SIZE
#define MAX_MAX_SIZE	(INT32_MAX)
#endif


#define MAX_TOKENS          10
#define LINE_LEN           300

/* some Minix specific types that do not conflict with Posix */
#ifndef block_t
typedef uint32_t block_t;	/* block number */
#endif
#ifndef zone_t
typedef uint32_t zone_t;	/* zone number */
#endif
#ifndef bit_t
typedef uint32_t bit_t;		/* bit number in a bit map */
#endif
#ifndef bitchunk_t
typedef uint32_t bitchunk_t;	/* collection of bits in a bitmap */
#endif

struct fs_size {
  ino_t inocount; /* amount of inodes */
  zone_t zonecount; /* amount of zones */
  block_t blockcount; /* amount of blocks */
};

extern char *optarg;
extern int optind;

block_t nrblocks;
int zone_per_block, zone_shift = 0;
zone_t next_zone, zoff, nr_indirzones;
int inodes_per_block, indir_per_block, indir_per_zone;
unsigned int zone_size;
ino_t nrinodes, inode_offset, next_inode;

/* Which format is being written, and the shape of an inode in it.  V3 is
 * the default and is what the ramdisk is made of; -4 asks for V4.
 */
int fs_version = 3;
size_t inode_size;
unsigned nr_dzones, nr_tzones, nr_levels;

/* The journal: how many blocks of it, and which inode owns them.  Only V4
 * has one; -J says how big, -J 0 says none.
 */
long journal_blocks = -1;	/* -1: not asked for, so use a default */
ino_t journal_inum = 0;
uint8_t fs_uuid[16];

/* An inode as this program handles it: as wide as the wider format, so
 * that the code that fills one in does not have to know which is being
 * written.  ino_get() and ino_put() convert.
 */
struct gen_inode {
	uint16_t mode;
	uint32_t nlinks;
	uint32_t uid;
	uint32_t gid;
	uint64_t size;
	int64_t atime, mtime, ctime, btime;
	uint32_t zone[MFS4_NR_TZONES];
};
int lct = 0, fd, print = 0;
int simple = 0, dflag = 0, verbose = 0;
int donttest;			/* skip test if it fits on medium */
char *progname;
uint64_t fs_offset_bytes, fs_offset_blocks, written_fs_size = 0;

time_t current_time;
char *zero;
unsigned char *umap_array;	/* bit map tells if block read yet */
size_t umap_array_elements;
block_t zone_map;		/* where is zone map? (depends on # inodes) */
#ifndef MFS_STATIC_BLOCK_SIZE
size_t block_size;
#else
#define block_size	MFS_STATIC_BLOCK_SIZE
#endif

FILE *proto;

int main(int argc, char **argv);
static void ino_get(ino_t n, struct gen_inode *gi);
static void ino_put(ino_t n, const struct gen_inode *gi);
static void map_insert(uint32_t *zone, uint64_t index, zone_t z);
static zone_t map_get(const uint32_t *zone, uint64_t index);
static void enter_dir_v4(ino_t parent, char const *name, ino_t child,
	unsigned type);
static void make_journal(ino_t inum, unsigned int nblocks);
static unsigned dt_of_mode(uint16_t mode);
void detect_fs_size(struct fs_size * fssize);
void sizeup_dir(struct fs_size * fssize);
block_t sizeup(char *device);
static int bitmapsize(bit_t nr_bits, size_t blk_size);
void super(zone_t zones, ino_t inodes);
void rootdir(ino_t inode);
void enter_symlink(ino_t inode, char *link);
int dir_try_enter(zone_t z, ino_t child, char const *name);
void eat_dir(ino_t parent);
void eat_file(ino_t inode, int f);
void enter_dir(ino_t parent, char const *name, ino_t child);
void add_zone(ino_t n, zone_t z, size_t bytes, time_t cur_time);
void incr_link(ino_t n);
void incr_size(ino_t n, size_t count);
static ino_t alloc_inode(int mode, int usrid, int grpid);
static zone_t alloc_zone(void);
void insert_bit(block_t block, bit_t bit);
int mode_con(char *p);
void get_line(char line[LINE_LEN], char *parse[MAX_TOKENS]);
void check_mtab(const char *devname);
time_t file_time(int f);
__dead void pexit(char const *s, ...) __printflike(1,2);
void *alloc_block(void);
void print_fs(void);
int read_and_set(block_t n);
void special(char *string, int insertmode);
__dead void usage(void);
void get_block(block_t n, void *buf);
void get_super_block(void *buf);
void put_block(block_t n, void *buf);
static uint64_t mkfs_seek(uint64_t pos, int whence);
static ssize_t mkfs_write(void * buf, size_t count);

/*================================================================
 *                    mkfs  -  make filesystem
 *===============================================================*/
int
main(int argc, char *argv[])
{
  int nread, mode, usrid, grpid, ch, extra_space_percent, Tflag = 0;
  block_t blocks, maxblocks, bblocks;
  ino_t inodes, root_inum;
  char *token[MAX_TOKENS], line[LINE_LEN], *sfx;
  struct fs_size fssize;
  int insertmode = 0;

  progname = argv[0];

  /* Process switches. */
  blocks = 0;
  inodes = 0;
  bblocks = 0;
#ifndef MFS_STATIC_BLOCK_SIZE
  block_size = 0;
#endif
  zone_shift = 0;
  extra_space_percent = 0;
  while ((ch = getopt(argc, argv, "34B:J:b:di:ltvx:z:I:T:")) != EOF)
	switch (ch) {
	    case '3':
		fs_version = 3;
		break;
	    case '4':
		fs_version = 4;
		break;
	    case 'J':
		journal_blocks = strtol(optarg, (char **) NULL, 0);
		if (journal_blocks < 0) usage();
		break;
#ifndef MFS_STATIC_BLOCK_SIZE
	    case 'B':
		block_size = strtoul(optarg, &sfx, 0);
		switch(*sfx) {
		case 'b': case 'B': /* bytes; NetBSD-compatible */
		case '\0':	break;
		case 'K':
		case 'k':	block_size*=1024; break;
		case 's': 	block_size*=SECTOR_SIZE; break;
		default:	usage();
		}
		break;
#else
	    case 'B':
		if (block_size != strtoul(optarg, (char **) NULL, 0))
			errx(4, "block size must be exactly %d bytes",
			    MFS_STATIC_BLOCK_SIZE);
		break;
		(void)sfx;	/* shut up warnings about unused variable...*/
#endif
	    case 'I':
		fs_offset_bytes = strtoul(optarg, (char **) NULL, 0);
		insertmode = 1;
		break;
	    case 'b':
		blocks = bblocks = strtoul(optarg, (char **) NULL, 0);
		break;
	    case 'T':
		Tflag = 1;
		current_time = strtoul(optarg, (char **) NULL, 0);
		break;
	    case 'd':
		dflag = 1;
		break;
	    case 'i':
		inodes = strtoul(optarg, (char **) NULL, 0);
		break;
	    case 'l':	print = 1;	break;
	    case 't':	donttest = 1;	break;
	    case 'v':	++verbose;	break;
	    case 'x':	extra_space_percent = atoi(optarg); break;
	    case 'z':	zone_shift = atoi(optarg);	break;
	    default:	usage();
	}

  if (argc == optind) usage();

  /* Get the current time, set it to the mod time of the binary of
   * mkfs itself when the -d flag is used. The 'current' time is put into
   * the i_mtimes of all the files.  This -d feature is useful when
   * producing a set of file systems, and one wants all the times to be
   * identical. First you set the time of the mkfs binary to what you
   * want, then go.
   */
  if(Tflag) {
    if(dflag)
	errx(1, "-T and -d both specify a time and so are mutually exclusive");
  } else if(dflag) {
	struct stat statbuf;
	if (stat(progname, &statbuf)) {
		err(1, "stat of itself");
	}
	current_time = statbuf.st_mtime;
  } else {
	  current_time = time((time_t *) 0);	/* time mkfs is being run */
  }

  /* Percentage of extra size must be nonnegative.
   * It can legitimately be bigger than 100 but has to make some sort of sense.
   */
  if(extra_space_percent < 0 || extra_space_percent > 2000) usage();

#ifdef DEFAULT_BLOCK_SIZE
  if(!block_size) block_size = DEFAULT_BLOCK_SIZE;
#endif
  if (block_size % SECTOR_SIZE)
	errx(4, "block size must be multiple of sector (%d bytes)", SECTOR_SIZE);
#ifdef MIN_BLOCK_SIZE
  if (block_size < MIN_BLOCK_SIZE)
	errx(4, "block size must be at least %d bytes", MIN_BLOCK_SIZE);
#endif
#ifdef MAX_BLOCK_SIZE
  if (block_size > MAX_BLOCK_SIZE)
	errx(4, "block size must be at most %d bytes", MAX_BLOCK_SIZE);
#endif
  /* The shape of an inode follows from the version, and everything else
   * that differs between the two follows from these.
   */
  if (fs_version == 4) {
	inode_size = MFS4_INODE_SIZE;
	nr_dzones = MFS4_NR_DZONES;
	nr_tzones = MFS4_NR_TZONES;
	nr_levels = MFS4_NR_LEVELS;

	if (block_size < MFS4_MIN_BLOCK_SIZE ||
	    block_size > MFS4_MAX_BLOCK_SIZE)
		errx(4, "V4 needs a block size between %d and %d bytes",
		    MFS4_MIN_BLOCK_SIZE, MFS4_MAX_BLOCK_SIZE);
  } else {
	inode_size = INODE_SIZE;
	nr_dzones = NR_DZONES;
	nr_tzones = NR_TZONES;
	nr_levels = MFS3_NR_LEVELS;
  }

  if(block_size % inode_size)
	errx(4, "block size must be a multiple of inode size (%d bytes)",
	    (int) inode_size);

  if(zone_shift < 0 || zone_shift > 14)
	errx(4, "zone_shift must be a small non-negative integer");
  /* V4 has no notion of a zone larger than a block. */
  if (fs_version == 4 && zone_shift != 0)
	errx(4, "V4 has no zones: -z is not for it");
  zone_per_block = 1 << zone_shift;	/* nr of blocks per zone */

  inodes_per_block = block_size / inode_size;
  indir_per_block = INDIRECTS(block_size);
  indir_per_zone = INDIRECTS(block_size) << zone_shift;
  /* number of file zones we can address directly and with a single indirect*/
  nr_indirzones = nr_dzones + indir_per_zone;
  zone_size = block_size << zone_shift;
  /* Checks for an overflow: only with very big block size */
  if (zone_size <= 0)
	errx(4, "Zones are too big for this program; smaller -B or -z, please!");

  /* now that the block size is known, do buffer allocations where
   * possible.
   */
  zero = alloc_block();

  fs_offset_blocks = roundup(fs_offset_bytes, block_size) / block_size;

  /* Determine the size of the device if not specified as -b or proto. */
  maxblocks = sizeup(argv[optind]);
  if (bblocks != 0 && bblocks + fs_offset_blocks > maxblocks && !insertmode) {
	errx(4, "Given size -b %d exceeds device capacity(%d)\n", bblocks, maxblocks);
  }

  if (argc - optind == 1 && bblocks == 0) {
  	blocks = maxblocks;
  	/* blocks == 0 is checked later, but leads to a funny way of
  	 * reporting a 0-sized device (displays usage).
  	 */
  	if(blocks < 1) {
  		errx(1, "zero size device.");
	}
  }

  /* The remaining args must be 'special proto', or just 'special' if the
   * no. of blocks has already been specified.
   */
  if (argc - optind != 2 && (argc - optind != 1 || blocks == 0)) usage();

  if (maxblocks && blocks > maxblocks && !insertmode) {
 	errx(1, "%s: number of blocks too large for device.", argv[optind]);
  }

  /* Check special. */
  check_mtab(argv[optind]);

  /* Check and start processing proto. */
  optarg = argv[++optind];
  if (optind < argc && (proto = fopen(optarg, "r")) != NULL) {
	/* Prototype file is readable. */
	lct = 1;
	get_line(line, token);	/* skip boot block info */

	/* Read the line with the block and inode counts. */
	get_line(line, token);
	if (bblocks == 0){
		blocks = strtol(token[0], (char **) NULL, 10);
	} else {
		if(bblocks < strtol(token[0], (char **) NULL, 10)) {
 			errx(1, "%s: number of blocks given as parameter(%d)"
				" is too small for given proto file(%ld).", 
				argv[optind], bblocks, 
				strtol(token[0], (char **) NULL, 10));
		};
	}
	inodes = strtol(token[1], (char **) NULL, 10);

	/* Process mode line for root directory. */
	get_line(line, token);
	mode = mode_con(token[0]);
	usrid = atoi(token[1]);
	grpid = atoi(token[2]);

	if(blocks <= 0 && inodes <= 0){
		detect_fs_size(&fssize);
		blocks = fssize.blockcount;
		inodes = fssize.inocount;
		blocks += blocks*extra_space_percent/100;
		inodes += inodes*extra_space_percent/100;
		/* The journal is a file of the file system, so a file
		 * system sized to its contents has to make room for it.
		 */
		if (fs_version == 4) {
			long jb = journal_blocks;

			if (jb < 0) {
				jb = blocks / 64;
				if (jb < 64) jb = 64;
				if (jb > 4096) jb = 4096;
				journal_blocks = jb;
			}
			blocks += jb + 8;	/* and its indirect blocks */
			inodes++;
		}
/* XXX is it OK to write on stdout? Use warn() instead? Also consider using verbose */
		fprintf(stderr, "dynamically sized filesystem: %u blocks, %u inodes\n",
		    (unsigned int) blocks, (unsigned int) inodes);
	}
  } else {
	lct = 0;
	if (optind < argc) {
		/* Maybe the prototype file is just a size.  Check. */
		blocks = strtoul(optarg, (char **) NULL, 0);
		if (blocks == 0) errx(2, "Can't open prototype file");
	}

	/* Make simple file system of the given size, using defaults. */
	mode = 040777;
	usrid = 0;
	grpid = 0;
	simple = 1;
  }

  if (inodes == 0) {
	long long kb = ((unsigned long long)blocks*block_size) / 1024;

	inodes = kb / 2;
	if (kb >= 100000) inodes = kb / 4;
	if (kb >= 1000000) inodes = kb / 6;
	if (kb >= 10000000) inodes = kb / 8;
	if (kb >= 100000000) inodes = kb / 10;
	if (kb >= 1000000000) inodes = kb / 12;
/* XXX check overflow: with very large number of blocks, this results in insanely large number of inodes */
/* XXX check underflow (if/when ino_t is signed), else the message below will look strange */

	/* round up to fill inode block */
	inodes += inodes_per_block - 1;
	inodes = inodes / inodes_per_block * inodes_per_block;
  }

  /*
   * The journal is a hundredth or so of the file system, within reason:
   * big enough that a transaction is never cramped, small enough that it
   * is not a tax on a small disk.  V3 has no journal at all.
   */
  if (fs_version != 4) {
	journal_blocks = 0;
  } else if (journal_blocks < 0) {
	journal_blocks = blocks / 64;
	if (journal_blocks < 64) journal_blocks = 64;
	if (journal_blocks > 4096) journal_blocks = 4096;
  }
  if (journal_blocks > 0 && journal_blocks < JOURNAL_MIN_BLOCKS)
	errx(1, "a journal of %ld blocks is too small (%d is the least)",
	    journal_blocks, JOURNAL_MIN_BLOCKS);

  if (blocks < 5) errx(1, "Block count too small");
  if (inodes < 1) errx(1, "Inode count too small");

  nrblocks = blocks;
  nrinodes = inodes;

  umap_array_elements = 1 + blocks/8;
  if(!(umap_array = malloc(umap_array_elements)))
	err(1, "can't allocate block bitmap (%u bytes).",
		(unsigned)umap_array_elements);

  /* Open special. */
  special(argv[--optind], insertmode);

  if (!donttest) {
	uint16_t *testb;
	ssize_t w;

	testb = alloc_block();

	/* Try writing the last block of partition or diskette. */
	mkfs_seek((uint64_t)(blocks - 1) * block_size, SEEK_SET);
	testb[0] = 0x3245;
	testb[1] = 0x11FF;
	testb[block_size/2-1] = 0x1F2F;
	w=mkfs_write(testb, block_size);
	sync();			/* flush write, so if error next read fails */
	mkfs_seek((uint64_t)(blocks - 1) * block_size, SEEK_SET);
	testb[0] = 0;
	testb[1] = 0;
	testb[block_size/2-1] = 0;
	nread = read(fd, testb, block_size);
	if (nread != block_size || testb[0] != 0x3245 || testb[1] != 0x11FF ||
	    testb[block_size/2-1] != 0x1F2F) {
		warn("nread = %d\n", nread);
		warnx("testb = 0x%x 0x%x 0x%x\n",
		    testb[0], testb[1], testb[block_size-1]);
		errx(1, "File system is too big for minor device (read)");
	}
	mkfs_seek((uint64_t)(blocks - 1) * block_size, SEEK_SET);
	testb[0] = 0;
	testb[1] = 0;
	testb[block_size/2-1] = 0;
	mkfs_write(testb, block_size);
	mkfs_seek(0L, SEEK_SET);
	free(testb);
  }

  /* Make the file-system */

  put_block(BOOT_BLOCK, zero);		/* Write a null boot block. */
  put_block(BOOT_BLOCK+1, zero);	/* Write another null block. */

  super(nrblocks >> zone_shift, inodes);

  root_inum = alloc_inode(mode, usrid, grpid);

  /* The journal comes next, so that it is inode 2 and its blocks are one
   * unbroken run near the start of the disk.
   */
  if (journal_blocks > 0) {
	journal_inum = alloc_inode(S_IFREG | 0600, 0, 0);
	make_journal(journal_inum, (unsigned int) journal_blocks);
  }

  rootdir(root_inum);
  if (simple == 0) eat_dir(root_inum);

  if (print) print_fs();
  else if (verbose > 1) {
	  if (zone_shift)
		fprintf(stderr, "%d inodes used.     %u zones (%u blocks) used.\n",
		    (int)next_inode-1, next_zone, next_zone*zone_per_block);
	  else
		fprintf(stderr, "%d inodes used.     %u zones used.\n",
		    (int)next_inode-1, next_zone);
  }

  if(insertmode) printf("%"PRIu64"\n", written_fs_size);

  return(0);

  /* NOTREACHED */
}				/* end main */

/*================================================================
 *        detect_fs_size  -  determine image size dynamically
 *===============================================================*/
void
detect_fs_size(struct fs_size * fssize)
{
  int prev_lct = lct;
  off_t point = ftell(proto);
  block_t initb;
  zone_t initz;

  fssize->inocount = 1;	/* root directory node */
  fssize->zonecount = 0;
  fssize->blockcount = 0;

  sizeup_dir(fssize);

  initb = bitmapsize(1 + fssize->inocount, block_size);
  initb += bitmapsize(fssize->zonecount, block_size);
  initb += START_BLOCK;
  initb += (fssize->inocount + inodes_per_block - 1) / inodes_per_block;
  initz = (initb + zone_per_block - 1) >> zone_shift;

  fssize->blockcount = initb+ fssize->zonecount;
  lct = prev_lct;
  fseek(proto, point, SEEK_SET);
}

void
sizeup_dir(struct fs_size * fssize)
{
  char *token[MAX_TOKENS], *p;
  char line[LINE_LEN]; 
  FILE *f;
  off_t size;
  int dir_entries = 2;
  size_t dir_bytes = 2 * MFS4_DIRENT_LEN(2);	/* "." and ".." */
  zone_t dir_zones = 0, fzones, indirects;

  while (1) {
	get_line(line, token);
	p = token[0];
	if (*p == '$') {
		if (fs_version == 4) {
			/* Records are variable-length, so the estimate is
			 * by the bytes the names actually need, with the
			 * longest possible record spared per block for the
			 * one that does not fit at its end.
			 */
			dir_zones = (dir_bytes + block_size - 1) / block_size;
			if (dir_zones == 0)
				dir_zones = 1;
		} else {
			dir_zones = (dir_entries / (NR_DIR_ENTRIES(block_size) * zone_per_block));
			if(dir_entries % (NR_DIR_ENTRIES(block_size) * zone_per_block))
				dir_zones++;
		}
		if(dir_zones > nr_dzones)
			dir_zones++;	/* Max single indir */
		fssize->zonecount += dir_zones;
		return;
	}

	p = token[1];
	fssize->inocount++;
	dir_entries++;
	dir_bytes += MFS4_DIRENT_LEN(strlen(token[0]));

	if (*p == 'd') {
		sizeup_dir(fssize);
	} else if (*p == 'b' || *p == 'c') {

	} else if (*p == 's') {
		/* A V4 target of 60 bytes or less lives in the inode. */
		if (fs_version != 4 ||
		    strlen(token[4]) > MFS4_FAST_SYMLINK_MAX)
			fssize->zonecount++;
	} else {
		if ((f = fopen(token[4], "rb")) == NULL) {
/* on minix natively, allow EACCES and skip the entry.
 * while crossbuilding, always fail on error.
 */
#ifdef __minix
			if(errno == EACCES)
				warn("dynamic sizing: can't open %s", token[4]);
			else
#endif
				err(1, "dynamic sizing: can't open %s", token[4]);
		} else if (fseek(f, 0, SEEK_END) < 0) {
			pexit("dynamic size detection failed: seek to end of %s",
			    token[4]);
		} else if ( (size = ftell(f)) == (off_t)(-1)) {
			pexit("dynamic size detection failed: can't tell size of %s",
			    token[4]);
		} else {
			fclose(f);
			fzones = roundup(size, zone_size) / zone_size;
			indirects = 0;
			/* XXX overflow? fzones is u32, size is potentially 64-bit */
			if (fzones > nr_dzones)
				indirects++; /* single indirect needed */
			if (fzones > nr_indirzones) {
				/* Each further group of 'indir_per_zone'
				 * needs one supplementary indirect zone:
				 */
				indirects += roundup(fzones - nr_indirzones,
				    indir_per_zone) / indir_per_zone;
				indirects++;	/* + double indirect needed!*/
			}
			fssize->zonecount += fzones + indirects;
		}
	}
  }
}

/*================================================================
 *                    sizeup  -  determine device size
 *===============================================================*/
block_t
sizeup(char * device)
{
  block_t d;
#if defined(__minix)
  uint64_t bytes, resize;
  uint32_t rem;
#else
  off_t size;
#endif


  if ((fd = open(device, O_RDONLY)) == -1) {
	if (errno != ENOENT)
		perror("sizeup open");
	return 0;
  }

#if defined(__minix)
  if(minix_sizeup(device, &bytes) < 0) {
       perror("sizeup");
       return 0;
  }

  d = (uint32_t)(bytes / block_size);
  rem = (uint32_t)(bytes % block_size);

  resize = ((uint64_t)d * block_size) + rem;
  if(resize != bytes) {
	/* Assume block_t is unsigned */
	d = (block_t)(-1ul);
	fprintf(stderr, "%s: truncating FS at %lu blocks\n",
		progname, (unsigned long)d);
  }
#else
  size = mkfs_seek(0, SEEK_END);
  /* Assume block_t is unsigned */
  if (size / block_size > (block_t)(-1ul)) {
	d = (block_t)(-1ul);
	fprintf(stderr, "%s: truncating FS at %lu blocks\n",
		progname, (unsigned long)d);
  } else
	d = size / block_size;
#endif

  return d;
}

/*
 * copied from fslib
 */
static int
bitmapsize(bit_t nr_bits, size_t blk_size)
{
  block_t nr_blocks;

  nr_blocks = nr_bits / FS_BITS_PER_BLOCK(blk_size);
  if (nr_blocks * FS_BITS_PER_BLOCK(blk_size) < nr_bits)
	++nr_blocks;
  return(nr_blocks);
}

/*================================================================
 *              inode and block-map access, either format
 *===============================================================*/

/* Read one inode into the shape this program works in. */
static void
ino_get(ino_t n, struct gen_inode *gi)
{
  block_t b;
  unsigned off, i;
  char *blk;

  b = ((n - 1) / inodes_per_block) + inode_offset;
  off = (n - 1) % inodes_per_block;

  blk = alloc_block();
  get_block(b, blk);
  memset(gi, 0, sizeof(*gi));

  if (fs_version == 4) {
	struct mfs4_inode *d = (struct mfs4_inode *) (blk + off * inode_size);

	gi->mode = d->i_mode;
	gi->nlinks = d->i_nlinks;
	gi->uid = d->i_uid;
	gi->gid = d->i_gid;
	gi->size = d->i_size;
	gi->atime = d->i_atime;
	gi->mtime = d->i_mtime;
	gi->ctime = d->i_ctime;
	gi->btime = d->i_btime;
	for (i = 0; i < MFS4_NR_TZONES; i++)
		gi->zone[i] = d->i_zone[i];
  } else {
	struct inode *d = (struct inode *) (blk + off * inode_size);

	gi->mode = d->i_mode;
	gi->nlinks = d->i_nlinks;
	gi->uid = d->i_uid;
	gi->gid = d->i_gid;
	gi->size = d->i_size;
	gi->atime = d->i_atime;
	gi->mtime = d->i_mtime;
	gi->ctime = d->i_ctime;
	for (i = 0; i < NR_TZONES; i++)
		gi->zone[i] = d->i_zone[i];
  }

  free(blk);
}

/* Write one inode back, narrowing it to what the format holds. */
static void
ino_put(ino_t n, const struct gen_inode *gi)
{
  block_t b;
  unsigned off, i;
  char *blk;

  b = ((n - 1) / inodes_per_block) + inode_offset;
  off = (n - 1) % inodes_per_block;

  blk = alloc_block();
  get_block(b, blk);

  if (fs_version == 4) {
	struct mfs4_inode *d = (struct mfs4_inode *) (blk + off * inode_size);

	d->i_mode = gi->mode;
	d->i_flags = 0;
	d->i_nlinks = gi->nlinks;
	d->i_uid = gi->uid;
	d->i_gid = gi->gid;
	d->i_size = gi->size;
	d->i_atime = gi->atime;
	d->i_mtime = gi->mtime;
	d->i_ctime = gi->ctime;
	d->i_btime = gi->btime;
	d->i_atime_nsec = d->i_mtime_nsec = d->i_ctime_nsec = 0;
	for (i = 0; i < MFS4_NR_TZONES; i++)
		d->i_zone[i] = gi->zone[i];
  } else {
	struct inode *d = (struct inode *) (blk + off * inode_size);

	d->i_mode = gi->mode;
	d->i_nlinks = gi->nlinks;
	if (d->i_nlinks != gi->nlinks)
		pexit("too many links for this version of Minix FS");
	d->i_uid = gi->uid;
	d->i_gid = gi->gid;
	d->i_size = gi->size;
	if (gi->size > MAX_MAX_SIZE)
		pexit("file too big for this version of Minix FS");
	d->i_atime = gi->atime;
	d->i_mtime = gi->mtime;
	d->i_ctime = gi->ctime;
	for (i = 0; i < NR_TZONES; i++)
		d->i_zone[i] = gi->zone[i];
  }

  put_block(b, blk);
  free(blk);
}

/* One entry of an indirect zone.  An indirect zone is one block unless -z
 * asked for larger zones, in which case its entries run over several.
 */
static zone_t
indir_get(zone_t z, uint64_t entry)
{
  uint32_t *blk = alloc_block();
  zone_t r;

  get_block((z << zone_shift) + entry / indir_per_block, blk);
  r = blk[entry % indir_per_block];
  free(blk);

  return r;
}

static void
indir_set(zone_t z, uint64_t entry, zone_t val)
{
  block_t b = (z << zone_shift) + entry / indir_per_block;
  uint32_t *blk = alloc_block();

  get_block(b, blk);
  blk[entry % indir_per_block] = val;
  put_block(b, blk);
  free(blk);
}

/*
 * Put block number 'z' at position 'index' of a file's block map, creating
 * whatever indirect blocks the way there needs.  Written for any number of
 * levels of indirection, because that number is exactly what differs between
 * the formats: V3 has two, V4 has three.
 */
static void
map_insert(uint32_t *zone, uint64_t index, zone_t z)
{
  uint64_t span = 1, ni = indir_per_zone, entry;
  unsigned lv, slot;
  zone_t cur, next;

  if (index < nr_dzones) {
	zone[index] = z;
	return;
  }
  index -= nr_dzones;

  for (lv = 1; lv <= nr_levels; lv++) {
	span *= ni;
	if (index < span) break;
	index -= span;
  }
  if (lv > nr_levels)
	pexit("file has grown beyond what this file system can address");

  slot = nr_dzones + lv - 1;
  if (zone[slot] == 0)
	zone[slot] = alloc_zone();
  cur = zone[slot];

  while (lv > 1) {
	span /= ni;
	entry = index / span;
	next = indir_get(cur, entry);
	if (next == 0) {
		next = alloc_zone();
		indir_set(cur, entry, next);
	}
	cur = next;
	index %= span;
	lv--;
  }

  indir_set(cur, index, z);
}

/* The other way: the block at 'index', or 0 if there is none. */
static zone_t
map_get(const uint32_t *zone, uint64_t index)
{
  uint64_t span = 1, ni = indir_per_zone, entry;
  unsigned lv, slot;
  zone_t cur;

  if (index < nr_dzones)
	return zone[index];
  index -= nr_dzones;

  for (lv = 1; lv <= nr_levels; lv++) {
	span *= ni;
	if (index < span) break;
	index -= span;
  }
  if (lv > nr_levels)
	return 0;

  slot = nr_dzones + lv - 1;
  cur = zone[slot];

  while (cur != 0 && lv > 1) {
	span /= ni;
	entry = index / span;
	cur = indir_get(cur, entry);
	index %= span;
	lv--;
  }

  if (cur == 0)
	return 0;

  return indir_get(cur, index);
}

/* The DT_* a directory entry records, from the mode of what it names. */
static unsigned
dt_of_mode(uint16_t mode)
{
  switch (mode & S_IFMT) {
  case S_IFREG:		return 8;	/* DT_REG */
  case S_IFDIR:		return 4;	/* DT_DIR */
  case S_IFLNK:		return 10;	/* DT_LNK */
  case S_IFCHR:		return 2;	/* DT_CHR */
  case S_IFBLK:		return 6;	/* DT_BLK */
  case S_IFIFO:		return 1;	/* DT_FIFO */
  case S_IFSOCK:	return 12;	/* DT_SOCK */
  default:		return 0;	/* DT_UNKNOWN */
  }
}

/*================================================================
 *                 super  -  construct a superblock
 *===============================================================*/

/* The V4 superblock.  Its feature words are all zero: a file system made
 * here has nothing in it that a V4 server does not know, which is what
 * lets an older server refuse a newer volume by its features rather than
 * by getting it wrong.
 */
static void
super_v4(zone_t blocks, ino_t inodes)
{
  struct mfs4_super *sup;
  block_t inodeblks, initblks, i;
  uint64_t ni, maxsize;
  void *buf;
  unsigned k;

  sup = buf = alloc_block();

  sup->s_magic = MFS4_SUPER_MAGIC;
  sup->s_disk_version = 0;
  sup->s_block_size = block_size;
  sup->s_ninodes = inodes;
  sup->s_nblocks = blocks;
  sup->s_inode_size = MFS4_INODE_SIZE;
  sup->s_flags = MFS4_FLAG_CLEAN;

  sup->s_imap_blocks = bitmapsize(1 + inodes, block_size);
  sup->s_zmap_blocks = bitmapsize(blocks, block_size);

  inode_offset = START_BLOCK + sup->s_imap_blocks + sup->s_zmap_blocks;
  inodeblks = (inodes + inodes_per_block - 1) / inodes_per_block;
  initblks = inode_offset + inodeblks;
  if (initblks >= blocks)
	errx(1, "bit maps too large");

  sup->s_firstdatablock = initblks;
  zoff = sup->s_firstdatablock - 1;

  /* What the block map of one inode can address: twelve blocks plus the
   * three trees.  At a 4 KB block that is about 4 TB.
   */
  ni = indir_per_zone;
  maxsize = ((uint64_t) nr_dzones + ni + ni * ni + ni * ni * ni) * block_size;
  sup->s_max_size = maxsize;

  sup->s_mkfs_time = current_time;

  /* The journal is inode 2 and its blocks are laid out right after the
   * root inode is allocated; the superblock only has to name it.
   */
  if (journal_blocks > 0) {
	sup->s_feature_compat |= MFS4_COMPAT_HAS_JOURNAL;
	sup->s_journal_inum = 2;
	sup->s_journal_blocks = (uint32_t) journal_blocks;
  }

  /* An identifier for the volume, made from what made it rather than from
   * a random source: two runs of mkfs with -d or -T produce the same image
   * byte for byte, and a uuid out of rand() would be the one thing that
   * did not.
   */
  for (k = 0; k < sizeof(sup->s_uuid); k++)
	sup->s_uuid[k] = (uint8_t) ((current_time >> ((k % 8) * 8)) ^
	    (blocks * (k + 1)) ^ (inodes << (k % 5)));
  sup->s_uuid[6] = (sup->s_uuid[6] & 0x0f) | 0x40;	/* version 4 */
  sup->s_uuid[8] = (sup->s_uuid[8] & 0x3f) | 0x80;	/* variant */

  /* The journal carries it too, so that one cannot be replayed onto the
   * wrong volume.
   */
  memcpy(fs_uuid, sup->s_uuid, sizeof(fs_uuid));

  if (verbose > 1)
	fprintf(stderr, "Super block values (V4):\n"
	    "\tnumber of inodes\t%12u\n"
	    "\tnumber of blocks\t%12u\n"
	    "\tinode bit map blocks\t%12u\n"
	    "\tblock bit map blocks\t%12u\n"
	    "\tfirst data block\t%12u\n"
	    "\tinode size\t\t%12u\n"
	    "\tmaximum file size\t%12llu\n"
	    "\tblock size\t\t%12u\n",
	    sup->s_ninodes, sup->s_nblocks, sup->s_imap_blocks,
	    sup->s_zmap_blocks, sup->s_firstdatablock, sup->s_inode_size,
	    (unsigned long long) sup->s_max_size, sup->s_block_size);

  mkfs_seek((off_t) SUPER_BLOCK_BYTES, SEEK_SET);
  mkfs_write(buf, SUPER_BLOCK_BYTES);

  /* Clear maps and inodes. */
  for (i = START_BLOCK; i < initblks; i++) put_block((block_t) i, zero);

  next_zone = sup->s_firstdatablock;
  next_inode = 1;

  zone_map = INODE_MAP + sup->s_imap_blocks;

  insert_bit(zone_map, 0);		/* bit zero is always allocated */
  insert_bit((block_t) INODE_MAP, 0);	/* inode zero is not used */

  free(buf);
}

void
super(zone_t zones, ino_t inodes)
{
  block_t inodeblks, initblks, i;
  unsigned long nb;
  long long ind_per_zone, zo;
  void *buf;
  struct super_block *sup;

  if (fs_version == 4) {
	super_v4(zones, inodes);
	return;
  }

  sup = buf = alloc_block();

#ifdef MFSFLAG_CLEAN
  /* The assumption is that mkfs will create a clean FS. */
  sup->s_flags = MFSFLAG_CLEAN;
#endif

  sup->s_ninodes = inodes;
  /* Check for overflow; cannot happen on V3 file systems */
  if(inodes != sup->s_ninodes)
	errx(1, "Too much inodes for that version of Minix FS.");
  sup->s_nzones = 0;	/* not used in V2 - 0 forces errors early */
  sup->s_zones = zones;
  /* Check for overflow; can only happen on V1 file systems */
  if(zones != sup->s_zones)
	errx(1, "Too much zones (blocks) for that version of Minix FS.");

#ifndef MFS_STATIC_BLOCK_SIZE
#define BIGGERBLOCKS "Please try a larger block size for an FS of this size."
#else
#define BIGGERBLOCKS "Please use MinixFS V3 for an FS of this size."
#endif
  sup->s_imap_blocks = nb = bitmapsize(1 + inodes, block_size);
  /* Checks for an overflow: nb is uint32_t while s_imap_blocks is of type
   * int16_t */
  if(sup->s_imap_blocks != nb) {
	errx(1, "too many inode bitmap blocks.\n" BIGGERBLOCKS);
  }
  sup->s_zmap_blocks = nb = bitmapsize(zones, block_size);
  /* Idem here check for overflow */
  if(nb != sup->s_zmap_blocks) {
	errx(1, "too many block bitmap blocks.\n" BIGGERBLOCKS);
  }
  inode_offset = START_BLOCK + sup->s_imap_blocks + sup->s_zmap_blocks;
  inodeblks = (inodes + inodes_per_block - 1) / inodes_per_block;
  initblks = inode_offset + inodeblks;
  sup->s_firstdatazone_old = nb =
	(initblks + (1 << zone_shift) - 1) >> zone_shift;
  if(nb >= zones) errx(1, "bit maps too large");
  if(nb != sup->s_firstdatazone_old) {
	/* The field is too small to store the value. Fortunately, the value
	 * can be computed from other fields. We set the on-disk field to zero
	 * to indicate that it must not be used. Eventually, we can always set
	 * the on-disk field to zero, and stop using it.
	 */
	sup->s_firstdatazone_old = 0;
  }
  sup->s_firstdatazone = nb;
  zoff = sup->s_firstdatazone - 1;
  sup->s_log_zone_size = zone_shift;
  sup->s_magic = SUPER_MAGIC;
#ifdef MFS_SUPER_BLOCK_SIZE
  sup->s_block_size = block_size;
  /* Check for overflow */
  if(block_size != sup->MFS_SUPER_BLOCK_SIZE)
	errx(1, "block_size too large.");
  sup->s_disk_version = 0;
#endif

  ind_per_zone = (long long) indir_per_zone;
  zo = NR_DZONES + ind_per_zone + ind_per_zone*ind_per_zone;
#ifndef MAX_MAX_SIZE
#define MAX_MAX_SIZE 	(INT32_MAX)
#endif
  if(MAX_MAX_SIZE/block_size < zo) {
	sup->s_max_size = MAX_MAX_SIZE;
  }
  else {
	sup->s_max_size = zo * block_size;
  }

  if (verbose>1) {
	fprintf(stderr, "Super block values:\n"
	    "\tnumber of inodes\t%12d\n"
	    "\tnumber of zones \t%12d\n"
	    "\tinode bit map blocks\t%12d\n"
	    "\tzone bit map blocks\t%12d\n"
	    "\tfirst data zone \t%12d\n"
	    "\tblocks per zone shift\t%12d\n"
	    "\tmaximum file size\t%12d\n"
	    "\tmagic number\t\t%#12X\n",
	    sup->s_ninodes, sup->s_zones,
	    sup->s_imap_blocks, sup->s_zmap_blocks, sup->s_firstdatazone,
	    sup->s_log_zone_size, sup->s_max_size, sup->s_magic);
#ifdef MFS_SUPER_BLOCK_SIZE
	fprintf(stderr, "\tblock size\t\t%12d\n", sup->s_block_size);
#endif
  }

  mkfs_seek((off_t) SUPER_BLOCK_BYTES, SEEK_SET);
  mkfs_write(buf, SUPER_BLOCK_BYTES);

  /* Clear maps and inodes. */
  for (i = START_BLOCK; i < initblks; i++) put_block((block_t) i, zero);

  next_zone = sup->s_firstdatazone;
  next_inode = 1;

  zone_map = INODE_MAP + sup->s_imap_blocks;

  insert_bit(zone_map, 0);	/* bit zero must always be allocated */
  insert_bit((block_t) INODE_MAP, 0);	/* inode zero not used but
					 * must be allocated */

  free(buf);
}


/*================================================================
 *          make_journal  -  the file the journal lives in
 *===============================================================*/
static void
make_journal(ino_t inum, unsigned int nblocks)
{
/* Give the journal inode a run of blocks and put a journal superblock at
 * the front of it.
 *
 * The run has to be unbroken: the file system server hands the block cache
 * a first block and a count, because the journal is meant to serve file
 * systems whose block maps it does not know how to read.  So the blocks are
 * taken first, all of them, and only then put into the inode - an indirect
 * block allocated along the way would otherwise land in the middle of the
 * run.
 *
 * Nothing links to this inode.  It is reserved, as ext3's journal inode is:
 * the superblock names it, and that is what it is found by.
 */
  struct gen_inode gi;
  struct journal_super *js;
  zone_t first, z;
  unsigned int i;
  void *buf;

  first = next_zone;
  for (i = 0; i < nblocks; i++) {
	z = alloc_zone();
	if (z != first + i)
		pexit("the journal did not come out as one run of blocks");
  }

  ino_get(inum, &gi);
  for (i = 0; i < nblocks; i++)
	map_insert(gi.zone, i, first + i);
  gi.size = (uint64_t) nblocks * block_size;
  gi.nlinks = 1;
  gi.mtime = gi.atime = gi.ctime = gi.btime = current_time;
  ino_put(inum, &gi);

  buf = alloc_block();
  js = buf;
  js->js_magic = JOURNAL_MAGIC;
  js->js_version = JOURNAL_VERSION;
  js->js_block_size = (uint32_t) block_size;
  js->js_nblocks = nblocks;
  js->js_start = 0;		/* nothing to replay on a new file system */
  js->js_sequence = 1;
  memcpy(js->js_uuid, fs_uuid, sizeof(js->js_uuid));
  put_block((block_t) first, buf);
  free(buf);

  if (verbose)
	fprintf(stderr, "journal: inode %u, %u blocks at %u\n",
	    (unsigned int) inum, nblocks, (unsigned int) first);
}

/*================================================================
 *              rootdir  -  install the root directory
 *===============================================================*/
void
rootdir(ino_t inode)
{
  zone_t z;

  /* On V4 the first block of a directory is made by the code that puts a
   * name in it, because a block has to be laid out as records before it
   * means anything.
   */
  if (fs_version != 4) {
	z = alloc_zone();
	add_zone(inode, z, 2 * sizeof(struct direct), current_time);
  }

  enter_dir(inode, ".", inode);
  enter_dir(inode, "..", inode);
  incr_link(inode);
  incr_link(inode);
}

void
enter_symlink(ino_t inode, char *lnk)
{
  zone_t z;
  size_t len;
  char *buf;

  len = strlen(lnk);

  /* A V4 target of 60 bytes or less lives in the inode itself. */
  if (fs_version == 4 && len <= MFS4_FAST_SYMLINK_MAX) {
	struct gen_inode gi;

	ino_get(inode, &gi);
	memcpy(gi.zone, lnk, len);
	gi.size = len;
	gi.mtime = gi.atime = gi.ctime = current_time;
	ino_put(inode, &gi);
	return;
  }

  buf = alloc_block();
  z = alloc_zone();
  if (len >= block_size)
	pexit("symlink too long, max length is %u", (unsigned)block_size - 1);
  strcpy(buf, lnk);
  put_block((z << zone_shift), buf);

  add_zone(inode, z, len, current_time);

  free(buf);
}


/*================================================================
 *	    eat_dir  -  recursively install directory
 *===============================================================*/
void
eat_dir(ino_t parent)
{
  /* Read prototype lines and set up directory. Recurse if need be. */
  char *token[MAX_TOKENS], *p;
  char line[LINE_LEN];
  int mode, usrid, grpid, maj, min, f;
  ino_t n;
  zone_t z;
  size_t size;

  while (1) {
	get_line(line, token);
	p = token[0];
	if (*p == '$') return;
	p = token[1];
	mode = mode_con(p);
	usrid = atoi(token[2]);
	grpid = atoi(token[3]);
	n = alloc_inode(mode, usrid, grpid);

	/* Enter name in directory and update directory's size.  On V4 the
	 * size of a directory is whole blocks, and enter_dir keeps it.
	 */
	enter_dir(parent, token[0], n);
	if (fs_version != 4)
		incr_size(parent, sizeof(struct direct));

	/* Check to see if file is directory or special. */
	incr_link(n);
	if (*p == 'd') {
		/* This is a directory. */
		if (fs_version != 4) {
			z = alloc_zone();  /* zone for new directory */
			add_zone(n, z, 2 * sizeof(struct direct),
			    current_time);
		}
		enter_dir(n, ".", n);
		enter_dir(n, "..", parent);
		incr_link(parent);
		incr_link(n);
		eat_dir(n);
	} else if (*p == 'b' || *p == 'c') {
		/* Special file. */
		maj = atoi(token[4]);
		min = atoi(token[5]);
		size = 0;
		if (token[6]) size = atoi(token[6]);
		size = block_size * size;
		add_zone(n, (zone_t) (makedev(maj,min)), size, current_time);
	} else if (*p == 's') {
		enter_symlink(n, token[4]);
	} else {
		/* Regular file. Go read it. */
		if ((f = open(token[4], O_RDONLY)) < 0) {
/* on minix natively, allow EACCES and skip the entry.
 * while crossbuilding, always fail on error.
 */
#ifdef __minix
			if(errno == EACCES)
				warn("Can't open %s", token[4]);
			else
#endif
				err(1, "Can't open %s", token[4]);
		} else {
			eat_file(n, f);
		}
	}
  }

}

/*================================================================
 * 		eat_file  -  copy file to MINIX
 *===============================================================*/
/* Zonesize >= blocksize */
void
eat_file(ino_t inode, int f)
{
  int ct = 0, i, j;
  zone_t z = 0;
  char *buf;
  time_t timeval;

  buf = alloc_block();

  do {
	for (i = 0, j = 0; i < zone_per_block; i++, j += ct) {
		memset(buf, 0, block_size);
		if ((ct = read(f, buf, block_size)) > 0) {
			if (i == 0) z = alloc_zone();
			put_block((z << zone_shift) + i, buf);
		}
	}
	timeval = (dflag ? current_time : file_time(f));
	if (ct) add_zone(inode, z, (size_t) j, timeval);
  } while (ct == block_size);
  close(f);
  free(buf);
}

int
dir_try_enter(zone_t z, ino_t child, char const *name)
{
  struct direct *dir_entry = alloc_block();
  int r = 0;
  block_t b;
  int i, l;

  b = z << zone_shift;
  for (l = 0; l < zone_per_block; l++, b++) {
	get_block(b, dir_entry);

	for (i = 0; i < NR_DIR_ENTRIES(block_size); i++)
		if (!dir_entry[i].d_ino)
			break;

	if(i < NR_DIR_ENTRIES(block_size)) {
		r = 1;
		dir_entry[i].d_ino = child;
		assert(sizeof(dir_entry[i].d_name) == MFS_DIRSIZ);
		if (verbose && strlen(name) > MFS_DIRSIZ)
			fprintf(stderr, "File name %s is too long, truncated\n", name);
		strncpy(dir_entry[i].d_name, name, MFS_DIRSIZ);
		put_block(b, dir_entry);
		break;
	}
  }

  free(dir_entry);

  return r;
}

/*
 * Put one record into a V4 directory block that has room for it.  A record
 * in use is split, keeping what its own name needs; a free record is taken
 * whole, so that no gap too small for any name is left behind.  This is the
 * same rule the server follows, and the two have to agree on it.
 */
static void
dirent_place(struct mfs4_dirent *dp, ino_t child, char const *name,
	size_t namelen, unsigned type)
{
  struct mfs4_dirent *nw;

  if (dp->d_ino != 0) {
	unsigned used = MFS4_DIRENT_LEN(dp->d_name_len);

	nw = (struct mfs4_dirent *) ((char *) dp + used);
	nw->d_rec_len = dp->d_rec_len - used;
	dp->d_rec_len = used;
	dp = nw;
  }

  dp->d_ino = (uint32_t) child;
  dp->d_name_len = (uint8_t) namelen;
  dp->d_type = (uint8_t) type;
  memcpy(dp->d_name, name, namelen);
  memset(dp->d_name + namelen, 0,
      dp->d_rec_len - MFS4_DIRENT_HDR - namelen);
}

static void
enter_dir_v4(ino_t parent, char const *name, ino_t child, unsigned type)
{
  struct gen_inode gi, child_gi;
  struct mfs4_dirent *dp;
  uint64_t index, nblocks;
  size_t namelen, need;
  unsigned off;
  char *blk;
  zone_t z;
  int placed = 0;

  namelen = strlen(name);
  if (namelen == 0 || namelen > MFS4_NAME_MAX)
	pexit("directory entry name of %u bytes", (unsigned) namelen);
  need = MFS4_DIRENT_LEN(namelen);

  /* The type is a property of what the name names, so it is read from
   * there rather than passed down through every caller.
   */
  if (type == (unsigned) -1) {
	ino_get(child, &child_gi);
	type = dt_of_mode(child_gi.mode);
  }

  ino_get(parent, &gi);
  nblocks = gi.size / block_size;
  blk = alloc_block();

  for (index = 0; index < nblocks && !placed; index++) {
	z = map_get(gi.zone, index);
	if (z == 0)
		pexit("hole in a directory being built");

	get_block(z << zone_shift, blk);

	for (off = 0; off + MFS4_DIRENT_HDR <= block_size;
	     off += dp->d_rec_len) {
		dp = (struct mfs4_dirent *) (blk + off);

		if (dp->d_rec_len < MFS4_DIRENT_HDR ||
		    off + dp->d_rec_len > block_size)
			pexit("corrupt directory record while building");

		if (dp->d_ino == 0) {
			if (dp->d_rec_len < need) continue;
		} else if (dp->d_rec_len - MFS4_DIRENT_LEN(dp->d_name_len) <
		    need) {
			continue;
		}

		dirent_place(dp, child, name, namelen, type);
		put_block(z << zone_shift, blk);
		placed = 1;
		break;
	}
  }

  if (!placed) {
	/* The directory grows by a block, which starts as one free record
	 * covering all of it.
	 */
	z = alloc_zone();
	map_insert(gi.zone, nblocks, z);

	memset(blk, 0, block_size);
	dp = (struct mfs4_dirent *) blk;
	dp->d_rec_len = block_size;
	dirent_place(dp, child, name, namelen, type);
	put_block(z << zone_shift, blk);

	gi.size += block_size;
  }

  gi.mtime = gi.ctime = gi.atime = current_time;
  ino_put(parent, &gi);

  free(blk);
}

/*================================================================
 *	    directory & inode management assist group
 *===============================================================*/
void
enter_dir(ino_t parent, char const *name, ino_t child)
{
  /* Enter child in parent directory */
  struct gen_inode gi;
  uint64_t index;
  zone_t z;

  if (fs_version == 4) {
	enter_dir_v4(parent, name, child, (unsigned) -1);
	return;
  }

  assert(!(block_size % sizeof(struct direct)));

  ino_get(parent, &gi);

  for (index = 0; ; index++) {
	z = map_get(gi.zone, index);

	if (z == 0) {
		/* The directory is full: give it another block, which is
		 * empty and will therefore take the entry.
		 */
		z = alloc_zone();
		map_insert(gi.zone, index, z);
		ino_put(parent, &gi);
	}

	if (dir_try_enter(z, child, __UNCONST(name)))
		return;
  }
}


void
add_zone(ino_t n, zone_t z, size_t bytes, time_t mtime)
{
  /* Add zone z to inode n. The file has grown by 'bytes' bytes. */
  struct gen_inode gi;
  uint64_t index;

  ino_get(n, &gi);

  /* Files are written out in order, so the zone being added is the one
   * after those the file already has.
   */
  index = gi.size / zone_size;

  gi.size += bytes;
  gi.mtime = mtime;
#ifndef MFS_INODE_ONLY_MTIME /* V1 file systems did not have them... */
  gi.atime = gi.ctime = current_time;
#endif
  if (fs_version == 4 && gi.btime == 0)
	gi.btime = current_time;

  map_insert(gi.zone, index, z);

  ino_put(n, &gi);
}


/* Increment the link count to inode n */
void
incr_link(ino_t n)
{
  struct gen_inode gi;
  static int enter = 0;

  if (enter++) pexit("internal error: recursive call to incr_link()");

  ino_get(n, &gi);
  gi.nlinks++;
  if (gi.nlinks == 0)
	pexit("Too many links to a directory");
  ino_put(n, &gi);

  enter = 0;
}


/* Increment the file-size in inode n */
void
incr_size(ino_t n, size_t count)
{
  struct gen_inode gi;

  ino_get(n, &gi);
  gi.size += count;
  ino_put(n, &gi);
}


/*================================================================
 * 	 	     allocation assist group
 *===============================================================*/
static ino_t
alloc_inode(int mode, int usrid, int grpid)
{
  struct gen_inode gi;
  ino_t num;

  num = next_inode++;
  if (num > nrinodes) {
  	pexit("File system does not have enough inodes (only %llu)", nrinodes);
  }

  ino_get(num, &gi);
  if (gi.mode) {
	pexit("allocation new inode %llu with non-zero mode - this cannot happen",
		num);
  }

  gi.mode = mode;
  gi.uid = usrid;
  gi.gid = grpid;
  gi.btime = current_time;
  if (verbose && ((int) gi.uid != usrid || (int) gi.gid != grpid))
	fprintf(stderr, "Uid/gid %d.%d do not fit within inode, truncated\n", usrid, grpid);
  ino_put(num, &gi);

  /* Set the bit in the bit map. */
  insert_bit((block_t) INODE_MAP, num);
  return(num);
}


/* Allocate a new zone */
static zone_t
alloc_zone(void)
{
  /* Works for zone > block */
  block_t b;
  int i;
  zone_t z;

  z = next_zone++;
  b = z << zone_shift;
  if (b > nrblocks - zone_per_block)
	pexit("File system not big enough for all the files");
  for (i = 0; i < zone_per_block; i++)
	put_block(b + i, zero);	/* give an empty zone */

  insert_bit(zone_map, z - zoff);
  return z;
}


/* Insert one bit into the bitmap */
void
insert_bit(block_t map, bit_t bit)
{
  int boff, w, s;
  unsigned int bits_per_block;
  block_t map_block;
  bitchunk_t *buf;

  buf = alloc_block();

  bits_per_block = FS_BITS_PER_BLOCK(block_size);
  map_block = map + bit / bits_per_block;
  if (map_block >= inode_offset)
	pexit("insertbit invades inodes area - this cannot happen");
  boff = bit % bits_per_block;

  assert(boff >=0);
  assert(boff < FS_BITS_PER_BLOCK(block_size));
  get_block(map_block, buf);
  w = boff / FS_BITCHUNK_BITS;
  s = boff % FS_BITCHUNK_BITS;
  buf[w] |= (1 << s);
  put_block(map_block, buf);

  free(buf);
}


/*================================================================
 * 		proto-file processing assist group
 *===============================================================*/
int mode_con(char *p)
{
  /* Convert string to mode */
  int o1, o2, o3, mode;
  char c1, c2, c3;

  c1 = *p++;
  c2 = *p++;
  c3 = *p++;
  o1 = *p++ - '0';
  o2 = *p++ - '0';
  o3 = *p++ - '0';
  mode = (o1 << 6) | (o2 << 3) | o3;
  if (c1 == 'd') mode |= S_IFDIR;
  if (c1 == 'b') mode |= S_IFBLK;
  if (c1 == 'c') mode |= S_IFCHR;
  if (c1 == 's') mode |= S_IFLNK;
  if (c1 == 'l') mode |= S_IFLNK;	/* just to be somewhat ls-compatible*/
/* XXX note: some other mkfs programs consider L to create hardlinks */
  if (c1 == '-') mode |= S_IFREG;
  if (c2 == 'u') mode |= S_ISUID;
  if (c3 == 'g') mode |= S_ISGID;
/* XXX There are no way to encode S_ISVTX */
  return(mode);
}

void
get_line(char line[LINE_LEN], char *parse[MAX_TOKENS])
{
  /* Read a line and break it up in tokens */
  int k;
  char c, *p;
  int d;

  for (k = 0; k < MAX_TOKENS; k++) parse[k] = 0;
  memset(line, 0, LINE_LEN);
  k = 0;
  p = line;
  while (1) {
	if (++k > LINE_LEN) pexit("Line too long");
	d = fgetc(proto);
	if (d == EOF) pexit("Unexpected end-of-file");
	*p = d;
	if (*p == ' ' || *p == '\t') *p = 0;
	if (*p == '\n') {
		lct++;
		*p++ = 0;
		*p = '\n';
		break;
	}
	p++;
  }

  k = 0;
  p = line;
  while (1) {
	c = *p++;
	if (c == '\n') return;
	if (c == 0) continue;
	parse[k++] = p - 1;
	do {
		c = *p++;
	} while (c != 0 && c != '\n');
  }
}


/*================================================================
 *			other stuff
 *===============================================================*/

/*
 * Check to see if the special file named 'device' is mounted.
 */
void
check_mtab(const char *device)		/* /dev/hd1 or whatever */
{
#if defined(__minix)
  int n, r;
  struct stat sb;
  char dev[PATH_MAX], mount_point[PATH_MAX],
	type[MNTNAMELEN], flags[MNTFLAGLEN];

  r= stat(device, &sb);
  if (r == -1)
  {
	if (errno == ENOENT)
		return;	/* Does not exist, and therefore not mounted. */
	err(1, "stat %s failed", device);
  }
  if (!S_ISBLK(sb.st_mode))
  {
	/* Not a block device and therefore not mounted. */
	return;
  }

  if (load_mtab(__UNCONST("mkfs")) < 0) return;
  while (1) {
	n = get_mtab_entry(dev, mount_point, type, flags);
	if (n < 0) return;
	if (strcmp(device, dev) == 0) {
		/* Can't mkfs on top of a mounted file system. */
		errx(1, "%s is mounted on %s", device, mount_point);
	}
  }
#else
	(void) device;	/* shut up warnings about unused variable... */
#endif
}


time_t
file_time(int f)
{
  struct stat statbuf;

  if (!fstat(f, &statbuf))
	return current_time;
  if (statbuf.st_mtime<0 || statbuf.st_mtime>(uint32_t)(-1))
	return current_time;
  return(statbuf.st_mtime);
}


__dead void
pexit(char const * s, ...)
{
  va_list va;

  va_start(va, s);
  vwarn(s, va);
  va_end(va);
  if (lct != 0)
	warnx("Line %d being processed when error detected.\n", lct);
  exit(2);
}


void *
alloc_block(void)
{
	void *buf;

	if(!(buf = malloc(block_size))) {
		err(1, "couldn't allocate filesystem buffer");
	}
	memset(buf, 0, block_size);

	return buf;
}

void
print_fs(void)
{
  int i, j;
  ino_t k;

  /* The dump below reads V3 inodes and fixed-size directory entries out of
   * raw blocks.  For V4 the same thing said through the accessors is a few
   * lines, and there is no second copy of the layout to keep right.
   */
  if (fs_version == 4) {
	struct gen_inode gi;
	ino_t n;

	printf("\nV4 file system, %u inodes, %u blocks, block size %u\n",
	    (unsigned) nrinodes, (unsigned) nrblocks, (unsigned) block_size);

	for (n = 1; n <= nrinodes; n++) {
		ino_get(n, &gi);
		if (gi.mode == 0) continue;
		printf("Inode %3u:  mode=%06o  uid=%2u  gid=%2u  size=%6llu"
		    "  zone[0]=%u\n", (unsigned) n, (unsigned) gi.mode,
		    (unsigned) gi.uid, (unsigned) gi.gid,
		    (unsigned long long) gi.size, (unsigned) gi.zone[0]);
	}

	printf("%d inodes used.     %u zones used.\n",
	    (int) next_inode - 1, next_zone);
	return;
  }
  struct inode *inode2;
  unsigned short *usbuf;
  block_t b;
  struct direct *dir;

  assert(inodes_per_block * sizeof(*inode2) == block_size);
  if(!(inode2 = alloc_block()))
	err(1, "couldn't allocate a block of inodes");

  assert(NR_DIR_ENTRIES(block_size)*sizeof(*dir) == block_size);
  if(!(dir = alloc_block()))
	err(1, "couldn't allocate a block of directory entries");

  usbuf = alloc_block();
  get_super_block(usbuf);
  printf("\nSuperblock: ");
  for (i = 0; i < 8; i++) printf("%06ho ", usbuf[i]);
  printf("\n            ");
  for (i = 0; i < 8; i++) printf("%#04hX ", usbuf[i]);
  printf("\n            ");
  for (i = 8; i < 15; i++) printf("%06ho ", usbuf[i]);
  printf("\n            ");
  for (i = 8; i < 15; i++) printf("%#04hX ", usbuf[i]);
  get_block((block_t) INODE_MAP, usbuf);
  printf("...\nInode map:  ");
  for (i = 0; i < 9; i++) printf("%06ho ", usbuf[i]);
  get_block((block_t) zone_map, usbuf);
  printf("...\nZone  map:  ");
  for (i = 0; i < 9; i++) printf("%06ho ", usbuf[i]);
  printf("...\n");

  free(usbuf);
  usbuf = NULL;

  k = 0;
  for (b = inode_offset; k < nrinodes; b++) {
	get_block(b, inode2);
	for (i = 0; i < inodes_per_block; i++) {
		k = inodes_per_block * (int) (b - inode_offset) + i + 1;
		/* Lint but OK */
		if (k > nrinodes) break;
		{
			if (inode2[i].i_mode != 0) {
				printf("Inode %3u:  mode=", (unsigned)k);
				printf("%06o", (unsigned)inode2[i].i_mode);
				printf("  uid=%2d  gid=%2d  size=",
					(int)inode2[i].i_uid, (int)inode2[i].i_gid);
				printf("%6ld", (long)inode2[i].i_size);
				printf("  zone[0]=%u\n", (unsigned)inode2[i].i_zone[0]);
			}
			if ((inode2[i].i_mode & S_IFMT) == S_IFDIR) {
				/* This is a directory */
				get_block(inode2[i].i_zone[0] << zone_shift, dir);
				for (j = 0; j < NR_DIR_ENTRIES(block_size); j++)
					if (dir[j].d_ino)
						printf("\tInode %2u: %s\n",
							(unsigned)dir[j].d_ino,
							dir[j].d_name);
			}
		}
	}
  }

  if (zone_shift)
	printf("%d inodes used.     %u zones (%u blocks) used.\n",
		(int)next_inode-1, next_zone, next_zone*zone_per_block);
  else
	printf("%d inodes used.     %u zones used.\n", (int)next_inode-1, next_zone);
  free(dir);
  free(inode2);
}


/*
 * The first time a block is read, it returns all 0s, unless there has
 * been a write.  This routine checks to see if a block has been accessed.
 */
int
read_and_set(block_t n)
{
  int w, s, mask, r;

  w = n / 8;
  
  assert(n < nrblocks);
  if(w >= umap_array_elements) {
	errx(1, "umap array too small - this can't happen");
  }
  s = n % 8;
  mask = 1 << s;
  r = (umap_array[w] & mask ? 1 : 0);
  umap_array[w] |= mask;
  return(r);
}

__dead void
usage(void)
{
  fprintf(stderr, "Usage: %s [-34dltv] [-b blocks] [-i inodes]\n"
	"\t[-z zone_shift] [-I offset] [-x extra] [-B blocksize] special [proto]\n"
	"\t-4 writes the V4 format, -3 the V3 one, which is the default\n"
	"\t-J blocks: size of the V4 journal; 0 for none\n",
      progname);
  exit(4);
}

void
special(char * string, int insertmode)
{
  int openmode = O_RDWR;
  if(!insertmode) openmode |= O_TRUNC;
  fd = open(string, O_RDWR | O_CREAT, 0644);
  if (fd < 0) err(1, "Can't open special file %s", string);
  mkfs_seek(0, SEEK_SET);
}



/* Read a block. */
void
get_block(block_t n, void *buf)
{
  ssize_t k;

  /* First access returns a zero block */
  if (read_and_set(n) == 0) {
	memcpy(buf, zero, block_size);
	return;
  }
  mkfs_seek((uint64_t)(n) * block_size, SEEK_SET);
  k = read(fd, buf, block_size);
  if (k != block_size)
	pexit("get_block couldn't read block #%u", (unsigned)n);
}

/* Read the super block. */
void
get_super_block(void *buf)
{
  ssize_t k;

  mkfs_seek((off_t) SUPER_BLOCK_BYTES, SEEK_SET);
  k = read(fd, buf, SUPER_BLOCK_BYTES);
  if (k != SUPER_BLOCK_BYTES)
	err(1, "get_super_block couldn't read super block");
}

/* Write a block. */
void
put_block(block_t n, void *buf)
{

  (void) read_and_set(n);

  mkfs_seek((uint64_t)(n) * block_size, SEEK_SET);
  mkfs_write(buf, block_size);
}

static ssize_t
mkfs_write(void * buf, size_t count)
{
	uint64_t fssize;
	ssize_t w;

	/* Perform & check write */
	w = write(fd, buf, count);
	if(w < 0)
		err(1, "mkfs_write: write failed");
	if(w != count)
		errx(1, "mkfs_write: short write: %zd != %zu", w, count);

	/* Check if this has made the FS any bigger; count bytes after offset */
	fssize = mkfs_seek(0, SEEK_CUR);

	assert(fssize >= fs_offset_bytes);
	fssize -= fs_offset_bytes;
	fssize = roundup(fssize, block_size);
	if(fssize > written_fs_size)
		written_fs_size = fssize;

	return w;
}

/* Seek to position in FS we're creating. */
static uint64_t
mkfs_seek(uint64_t pos, int whence)
{
	if(whence == SEEK_SET) pos += fs_offset_bytes;
	off_t newpos;
	if((newpos=lseek(fd, pos, whence)) == (off_t) -1)
		err(1, "mkfs_seek: lseek failed");
	return newpos;
}
