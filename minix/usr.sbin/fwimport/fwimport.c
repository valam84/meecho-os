/*
 * fwimport - copy firmware out of a FAT or ext4 partition, read only.
 *
 *	fwimport fat  DEV DIR TODIR		the tree under DIR, whole
 *	fwimport ext4 DEV TODIR PATH...		the files named
 *	fwimport [-v] ...
 *
 * Some machines keep the firmware their remote processors run on
 * partitions of their own: the Mi Pad 4 has the modem's, the Wi-Fi's and
 * the DSPs' on "modem" (FAT) and the GPU's on "vendor" (ext4), and those
 * files are Qualcomm's and Xiaomi's, not MEECHO's to ship.  MEECHO has no
 * file system server for either, so this takes a copy at the first boot
 * (port/clover/rc.clover) and the services read the copy from
 * /libdata/firmware.  DEV is a read-only window on the partition
 * (sdmmc's gptroN=).
 *
 * Only reading, and only what that needs: FAT12/16/32 with long names;
 * ext4 with extents, linear or htree directories (read linearly), 32- or
 * 64-byte group descriptors.  Nothing here writes to DEV.  Builds with a
 * host compiler too (port/test/fwimport/).
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int dev = -1;
static int verbose;
static unsigned long nfiles;
static unsigned long long nbytes;

static void
die(const char *fmt, ...)
{
	va_list ap;

	fprintf(stderr, "fwimport: ");
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, "\n");
	exit(1);
}

static void
readat(void *buf, size_t len, uint64_t off)
{
	ssize_t r;
	size_t done = 0;

	while (done < len) {
		r = pread(dev, (char *)buf + done, len - done,
		    (off_t)(off + done));
		if (r < 0) {
			if (errno == EINTR)
				continue;
			die("read at %llu: %s", (unsigned long long)off,
			    strerror(errno));
		}
		if (r == 0)
			die("read at %llu: past the end",
			    (unsigned long long)(off + done));
		done += (size_t)r;
	}
}

static void *
xmalloc(size_t n)
{
	void *p = malloc(n ? n : 1);

	if (p == NULL)
		die("out of memory (%zu bytes)", n);
	return p;
}

static unsigned
le16(const unsigned char *p)
{
	return p[0] | (unsigned)p[1] << 8;
}

static uint32_t
le32(const unsigned char *p)
{
	return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
	    (uint32_t)p[3] << 24;
}

/* A file being written: TODIR/NAME, created anew. */
static int
out_open(const char *path)
{
	int fd;

	(void)unlink(path);
	if ((fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0)
		die("%s: %s", path, strerror(errno));
	return fd;
}

static void
out_write(int fd, const char *path, const void *buf, size_t len)
{
	ssize_t r;
	size_t done = 0;

	while (done < len) {
		r = write(fd, (const char *)buf + done, len - done);
		if (r < 0) {
			if (errno == EINTR)
				continue;
			die("%s: %s", path, strerror(errno));
		}
		done += (size_t)r;
	}
}

static void
out_close(int fd, const char *path, uint64_t size)
{
	if (close(fd) != 0)
		die("%s: %s", path, strerror(errno));
	nfiles++;
	nbytes += size;
	if (verbose)
		printf("%s %llu\n", path, (unsigned long long)size);
}

static void
mkdir_p(const char *path)
{
	char buf[1024], *p;

	if (strlen(path) >= sizeof(buf))
		die("%s: path too long", path);
	strcpy(buf, path);
	for (p = buf + 1; ; p++) {
		if (*p == '/' || *p == '\0') {
			char c = *p;

			*p = '\0';
			if (mkdir(buf, 0755) != 0 && errno != EEXIST)
				die("%s: %s", buf, strerror(errno));
			if (c == '\0')
				break;
			*p = c;
		}
	}
}

/*
 * FAT.  The whole FAT is read once; directories and files are followed by
 * their cluster chains.  A name is its long name when it has one, else the
 * short name with the NT case bits, else the short name in lower case -
 * the case the services ask for.
 */
static struct {
	unsigned bps, spc, type;	/* type: 12, 16 or 32 */
	uint64_t fat_off, root_off, data_off;
	uint32_t root_entries, root_clus, nclus;
	unsigned char *fat;
} fat;

static uint32_t
fat_next(uint32_t c)
{
	uint32_t v;

	if (c < 2 || c >= fat.nclus + 2)
		die("FAT: cluster %lu out of range", (unsigned long)c);
	switch (fat.type) {
	case 12:
		v = le16(fat.fat + c + c / 2);
		v = (c & 1) ? v >> 4 : v & 0xfff;
		return v >= 0xff8 ? 0 : v;
	case 16:
		v = le16(fat.fat + 2 * c);
		return v >= 0xfff8 ? 0 : v;
	default:
		v = le32(fat.fat + 4 * c) & 0x0fffffff;
		return v >= 0x0ffffff8 ? 0 : v;
	}
}

static uint64_t
fat_clus_off(uint32_t c)
{
	return fat.data_off + (uint64_t)(c - 2) * fat.spc * fat.bps;
}

static void
fat_mount(void)
{
	unsigned char bs[512];
	uint32_t rsvd, nfats, fatsz, tot, rootsec, datasec;

	readat(bs, sizeof(bs), 0);
	if (bs[510] != 0x55 || bs[511] != 0xaa)
		die("FAT: no boot sector signature");
	fat.bps = le16(bs + 11);
	fat.spc = bs[13];
	rsvd = le16(bs + 14);
	nfats = bs[16];
	fat.root_entries = le16(bs + 17);
	tot = le16(bs + 19) ? le16(bs + 19) : le32(bs + 32);
	fatsz = le16(bs + 22) ? le16(bs + 22) : le32(bs + 36);
	if (fat.bps < 512 || fat.bps > 4096 || (fat.bps & (fat.bps - 1)) ||
	    fat.spc == 0 || (fat.spc & (fat.spc - 1)) || nfats == 0 ||
	    fatsz == 0 || rsvd == 0)
		die("FAT: not a FAT boot sector");
	rootsec = (fat.root_entries * 32 + fat.bps - 1) / fat.bps;
	datasec = tot - (rsvd + nfats * fatsz + rootsec);
	fat.nclus = datasec / fat.spc;
	fat.type = fat.nclus < 4085 ? 12 : fat.nclus < 65525 ? 16 : 32;
	fat.fat_off = (uint64_t)rsvd * fat.bps;
	fat.root_off = fat.fat_off + (uint64_t)nfats * fatsz * fat.bps;
	fat.data_off = fat.root_off + (uint64_t)rootsec * fat.bps;
	fat.root_clus = fat.type == 32 ? le32(bs + 44) : 0;
	fat.fat = xmalloc((size_t)fatsz * fat.bps);
	readat(fat.fat, (size_t)fatsz * fat.bps, fat.fat_off);
	if (verbose)
		printf("# FAT%u, %u-byte sectors, %u per cluster, "
		    "%lu clusters\n", fat.type, fat.bps, fat.spc,
		    (unsigned long)fat.nclus);
}

/* A directory's entries, whole: the root of FAT12/16, or a chain. */
static unsigned char *
fat_dir_read(uint32_t clus, size_t *lenp)
{
	size_t cs = (size_t)fat.spc * fat.bps, len = 0;
	unsigned char *buf;
	unsigned n = 0;
	uint32_t c;

	if (clus == 0 && fat.type != 32) {
		len = (size_t)fat.root_entries * 32;
		buf = xmalloc(len);
		readat(buf, len, fat.root_off);
		*lenp = len;
		return buf;
	}
	if (clus == 0)
		clus = fat.root_clus;
	for (c = clus; c != 0; c = fat_next(c))
		if (++n > fat.nclus)
			die("FAT: directory chain loops");
	buf = xmalloc(n * cs);
	for (c = clus; c != 0; c = fat_next(c)) {
		readat(buf + len, cs, fat_clus_off(c));
		len += cs;
	}
	*lenp = len;
	return buf;
}

struct fat_ent {
	char name[256];
	unsigned attr;
	uint32_t clus, size;
};

/*
 * Walk a directory: call fn for each entry other than ".", "..", the
 * volume label and deleted ones.  Stops early when fn says so.
 */
static int
fat_dir_walk(uint32_t clus, int (*fn)(const struct fat_ent *, void *),
    void *arg)
{
	unsigned char *buf, *e;
	size_t len, off;
	char lfn[256];
	int have_lfn = 0, r = 0;
	struct fat_ent ent;

	buf = fat_dir_read(clus, &len);
	memset(lfn, 0, sizeof(lfn));
	for (off = 0; off + 32 <= len; off += 32) {
		e = buf + off;
		if (e[0] == 0x00)
			break;
		if (e[0] == 0xe5) {
			have_lfn = 0;
			continue;
		}
		if (e[11] == 0x0f) {
			static const int at[13] =
			    { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };
			unsigned ord = e[0] & 0x3f, i, base, ch;

			if (ord == 0 || ord > 20) {
				have_lfn = 0;
				continue;
			}
			if (e[0] & 0x40) {
				memset(lfn, 0, sizeof(lfn));
				have_lfn = 1;
			}
			base = (ord - 1) * 13;
			for (i = 0; i < 13 && base + i < sizeof(lfn) - 1;
			    i++) {
				ch = le16(e + at[i]);
				if (ch == 0x0000 || ch == 0xffff)
					ch = 0;
				else if (ch >= 0x80)
					ch = '?';
				lfn[base + i] = (char)ch;
			}
			continue;
		}
		if (e[11] & 0x08) {		/* volume label */
			have_lfn = 0;
			continue;
		}
		memset(&ent, 0, sizeof(ent));
		if (have_lfn && lfn[0] != '\0') {
			strcpy(ent.name, lfn);
		} else {
			char base[9], ext[4];
			int i, lowb = e[12] & 0x08, lowe = e[12] & 0x10;

			memcpy(base, e, 8);
			base[8] = '\0';
			memcpy(ext, e + 8, 3);
			ext[3] = '\0';
			if ((unsigned char)base[0] == 0x05)
				base[0] = (char)0xe5;
			for (i = 7; i >= 0 && base[i] == ' '; i--)
				base[i] = '\0';
			for (i = 2; i >= 0 && ext[i] == ' '; i--)
				ext[i] = '\0';
			/* No case bits: lower case, what is asked for. */
			if (!lowb && !lowe)
				lowb = lowe = 1;
			for (i = 0; base[i]; i++)
				if (lowb)
					base[i] = tolower((unsigned char)base[i]);
			for (i = 0; ext[i]; i++)
				if (lowe)
					ext[i] = tolower((unsigned char)ext[i]);
			snprintf(ent.name, sizeof(ent.name), "%s%s%s", base,
			    ext[0] ? "." : "", ext);
		}
		have_lfn = 0;
		if (strcmp(ent.name, ".") == 0 || strcmp(ent.name, "..") == 0)
			continue;
		ent.attr = e[11];
		ent.clus = (fat.type == 32 ? (uint32_t)le16(e + 20) << 16 : 0) |
		    le16(e + 26);
		ent.size = le32(e + 28);
		if ((r = fn(&ent, arg)) != 0)
			break;
	}
	free(buf);
	return r;
}

struct fat_find {
	const char *name;
	struct fat_ent ent;
	int found;
};

static int
fat_find_fn(const struct fat_ent *e, void *arg)
{
	struct fat_find *f = arg;

	if (strcasecmp(e->name, f->name) != 0)
		return 0;
	f->ent = *e;
	f->found = 1;
	return 1;
}

static void
fat_copy_file(const struct fat_ent *e, const char *to)
{
	size_t cs = (size_t)fat.spc * fat.bps;
	unsigned char *buf = xmalloc(cs);
	uint32_t c = e->clus, left = e->size, n = 0;
	int fd = out_open(to);

	while (left > 0) {
		size_t chunk = left < cs ? left : cs;

		if (c == 0 || ++n > fat.nclus)
			die("%s: cluster chain shorter than the file", to);
		readat(buf, cs, fat_clus_off(c));
		out_write(fd, to, buf, chunk);
		left -= (uint32_t)chunk;
		c = left ? fat_next(c) : c;
	}
	free(buf);
	out_close(fd, to, e->size);
}

struct fat_tree {
	char to[1024];
	int depth;
};

static int fat_tree_fn(const struct fat_ent *, void *);

static void
fat_copy_tree(uint32_t clus, const char *to, int depth)
{
	struct fat_tree t;

	if (depth > 16)
		die("%s: directories nested too deep", to);
	mkdir_p(to);
	snprintf(t.to, sizeof(t.to), "%s", to);
	t.depth = depth;
	(void)fat_dir_walk(clus, fat_tree_fn, &t);
}

static int
fat_tree_fn(const struct fat_ent *e, void *arg)
{
	struct fat_tree *t = arg;
	char path[1024];

	if (strchr(e->name, '/') != NULL)
		return 0;
	if ((size_t)snprintf(path, sizeof(path), "%s/%s", t->to, e->name) >=
	    sizeof(path))
		die("%s/%s: path too long", t->to, e->name);
	if (e->attr & 0x10)
		fat_copy_tree(e->clus, path, t->depth + 1);
	else
		fat_copy_file(e, path);
	return 0;
}

static void
do_fat(const char *dir, const char *to)
{
	char buf[1024], *p, *save;
	uint32_t clus = 0;
	struct fat_find f;

	fat_mount();
	snprintf(buf, sizeof(buf), "%s", dir);
	for (p = strtok_r(buf, "/", &save); p != NULL;
	    p = strtok_r(NULL, "/", &save)) {
		memset(&f, 0, sizeof(f));
		f.name = p;
		(void)fat_dir_walk(clus, fat_find_fn, &f);
		if (!f.found)
			die("FAT: %s: no such directory (at %s)", dir, p);
		if (!(f.ent.attr & 0x10))
			die("FAT: %s: not a directory", dir);
		clus = f.ent.clus;
	}
	fat_copy_tree(clus, to, 0);
}

/*
 * ext4.  Inodes are found by their group's table; a file's blocks by its
 * extent tree.
 */
static struct {
	uint32_t bs, ipg, isz, dsz, first;
	int bits64;
	unsigned char *gd;
	uint32_t ngroups;
} e4;

#define EXT4_EXTENTS_FL		0x00080000
#define EXT4_INLINE_DATA_FL	0x10000000

struct e4_inode {
	unsigned mode;
	uint32_t flags;
	uint64_t size;
	unsigned char blk[60];
};

static void
e4_mount(void)
{
	unsigned char sb[1024];
	uint32_t blocks_lo, blocks_hi, bpg, incompat;
	uint64_t blocks;
	size_t gdlen;

	readat(sb, sizeof(sb), 1024);
	if (le16(sb + 56) != 0xef53)
		die("ext4: no superblock magic");
	e4.bs = 1024u << le32(sb + 24);
	e4.first = le32(sb + 20);
	bpg = le32(sb + 32);
	e4.ipg = le32(sb + 40);
	e4.isz = le16(sb + 88);
	incompat = le32(sb + 96);
	e4.bits64 = (incompat & 0x80) != 0;
	e4.dsz = e4.bits64 ? le16(sb + 254) : 32;
	if (e4.dsz < 32)
		e4.dsz = 32;
	blocks_lo = le32(sb + 4);
	blocks_hi = e4.bits64 ? le32(sb + 0x150) : 0;
	blocks = (uint64_t)blocks_hi << 32 | blocks_lo;
	if (e4.bs > 65536 || bpg == 0 || e4.ipg == 0 || e4.isz < 128)
		die("ext4: unreasonable superblock");
	e4.ngroups = (uint32_t)((blocks - e4.first + bpg - 1) / bpg);
	gdlen = (size_t)e4.ngroups * e4.dsz;
	e4.gd = xmalloc(gdlen);
	readat(e4.gd, gdlen, (uint64_t)(e4.first + 1) * e4.bs);
	if (verbose)
		printf("# ext4, %u-byte blocks, %u groups, %u-byte inodes, "
		    "%u-byte descriptors\n", e4.bs, e4.ngroups, e4.isz, e4.dsz);
}

static void
e4_inode(uint32_t ino, struct e4_inode *in)
{
	uint32_t g = (ino - 1) / e4.ipg, i = (ino - 1) % e4.ipg;
	const unsigned char *gd;
	unsigned char raw[256];
	uint64_t table;

	if (ino == 0 || g >= e4.ngroups)
		die("ext4: inode %lu out of range", (unsigned long)ino);
	gd = e4.gd + (size_t)g * e4.dsz;
	table = le32(gd + 8);
	if (e4.dsz >= 64)
		table |= (uint64_t)le32(gd + 0x28) << 32;
	readat(raw, 160, table * e4.bs + (uint64_t)i * e4.isz);
	in->mode = le16(raw + 0);
	in->size = le32(raw + 4) | (uint64_t)le32(raw + 108) << 32;
	in->flags = le32(raw + 32);
	memcpy(in->blk, raw + 40, 60);
}

/* The physical block of logical block lb, 0 for a hole. */
static uint64_t
e4_map(const struct e4_inode *in, uint32_t lb)
{
	static unsigned char node[65536];
	const unsigned char *h = in->blk;
	unsigned n, depth, i, level = 0;

	if (!(in->flags & EXT4_EXTENTS_FL))
		die("ext4: a file without extents (not supported)");
	for (;;) {
		if (le16(h) != 0xf30a)
			die("ext4: bad extent header");
		n = le16(h + 2);
		depth = le16(h + 6);
		if (depth == 0) {
			for (i = 0; i < n; i++) {
				const unsigned char *x = h + 12 + 12 * i;
				uint32_t start = le32(x);
				unsigned len = le16(x + 4);

				if (len > 32768)
					len -= 32768;	/* unwritten: zeros */
				if (lb >= start && lb < start + len) {
					if (le16(x + 4) > 32768)
						return 0;
					return ((uint64_t)le16(x + 6) << 32 |
					    le32(x + 8)) + (lb - start);
				}
			}
			return 0;
		}
		for (i = n; i > 0; i--)
			if (le32(h + 12 + 12 * (i - 1)) <= lb)
				break;
		if (i == 0)
			return 0;
		{
			const unsigned char *x = h + 12 + 12 * (i - 1);
			uint64_t leaf = (uint64_t)le16(x + 8) << 32 |
			    le32(x + 4);

			if (++level > 5)
				die("ext4: extent tree too deep");
			readat(node, e4.bs, leaf * e4.bs);
			h = node;
		}
	}
}

/* Look NAME up in directory inode DIR: its inode, or 0. */
static uint32_t
e4_lookup(uint32_t dir, const char *name)
{
	struct e4_inode in;
	unsigned char *blk = xmalloc(e4.bs);
	uint64_t nblk, b, pb;
	size_t nl = strlen(name);
	uint32_t found = 0;

	e4_inode(dir, &in);
	if ((in.mode & 0170000) != 0040000)
		die("ext4: inode %lu is not a directory", (unsigned long)dir);
	if (in.flags & EXT4_INLINE_DATA_FL)
		die("ext4: inline directory (not supported)");
	nblk = (in.size + e4.bs - 1) / e4.bs;
	for (b = 0; b < nblk && !found; b++) {
		size_t off = 0;

		if ((pb = e4_map(&in, (uint32_t)b)) == 0)
			continue;
		readat(blk, e4.bs, pb * e4.bs);
		while (off + 8 <= e4.bs) {
			uint32_t ino = le32(blk + off);
			unsigned rl = le16(blk + off + 4), l = blk[off + 6];

			if (rl < 8 || off + rl > e4.bs)
				break;
			if (ino != 0 && l == nl &&
			    memcmp(blk + off + 8, name, nl) == 0) {
				found = ino;
				break;
			}
			off += rl;
		}
	}
	free(blk);
	return found;
}

static void
e4_copy(const char *path, const char *todir)
{
	char buf[1024], to[1024], *p, *save, *last = NULL;
	uint32_t ino = 2;
	struct e4_inode in;
	unsigned char *blk;
	uint64_t b, nblk, pb, left;
	int fd;

	snprintf(buf, sizeof(buf), "%s", path);
	for (p = strtok_r(buf, "/", &save); p != NULL;
	    p = strtok_r(NULL, "/", &save)) {
		if ((ino = e4_lookup(ino, p)) == 0)
			die("ext4: %s: not found (at %s)", path, p);
		last = p;
	}
	if (last == NULL)
		die("ext4: %s: not a file", path);
	e4_inode(ino, &in);
	if ((in.mode & 0170000) != 0100000)
		die("ext4: %s: not a regular file", path);
	if (in.flags & EXT4_INLINE_DATA_FL)
		die("ext4: %s: inline data (not supported)", path);
	snprintf(to, sizeof(to), "%s/%s", todir, last);
	blk = xmalloc(e4.bs);
	fd = out_open(to);
	nblk = (in.size + e4.bs - 1) / e4.bs;
	left = in.size;
	for (b = 0; b < nblk; b++) {
		size_t chunk = left < e4.bs ? (size_t)left : e4.bs;

		if ((pb = e4_map(&in, (uint32_t)b)) == 0)
			memset(blk, 0, e4.bs);
		else
			readat(blk, e4.bs, pb * e4.bs);
		out_write(fd, to, blk, chunk);
		left -= chunk;
	}
	free(blk);
	out_close(fd, to, in.size);
}

static void
usage(void)
{
	fprintf(stderr,
	    "usage: fwimport [-v] fat DEV DIR TODIR\n"
	    "       fwimport [-v] ext4 DEV TODIR PATH...\n");
	exit(2);
}

int
main(int argc, char **argv)
{
	int c, i;

	while ((c = getopt(argc, argv, "v")) != -1) {
		if (c == 'v')
			verbose = 1;
		else
			usage();
	}
	argc -= optind;
	argv += optind;
	if (argc < 4)
		usage();
	if ((dev = open(argv[1], O_RDONLY)) < 0)
		die("%s: %s", argv[1], strerror(errno));
	if (strcmp(argv[0], "fat") == 0 && argc == 4) {
		do_fat(argv[2], argv[3]);
	} else if (strcmp(argv[0], "ext4") == 0) {
		e4_mount();
		mkdir_p(argv[2]);
		for (i = 3; i < argc; i++)
			e4_copy(argv[i], argv[2]);
	} else
		usage();
	printf("fwimport: %lu files, %llu bytes\n", nfiles, nbytes);
	return 0;
}
