/*
 * mkbootarchive - build the blob the AArch64 kernel is handed as an initrd.
 *
 * A device tree names one extra range and MINIX needs a dozen separate ELF
 * images, so the images are concatenated behind a small index and the kernel
 * takes the result apart in pre_init(). The format is <bootarchive.h>, which
 * both ends include; this is the writing end.
 *
 * Usage:
 *	mkbootarchive [-v] -o archive [name=]file ...
 *	mkbootarchive -t archive
 *
 * A plain file argument is stored under its basename. The name=file form is
 * for the times when the file on disk is not called what the kernel looks
 * for - a build puts binaries in per-program object directories, and the
 * kernel matches these names against its boot image table.
 *
 * Numbers go out little-endian whatever this host is, because the format
 * says so and the reading end is a kernel with no way to report a surprise.
 */

#include <sys/stat.h>
#include <sys/types.h>

#include <err.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <bootarchive.h>

/* One image on its way into the archive. */
struct image {
	const char *path;
	char name[BOOT_ARCHIVE_NAMELEN];
	uint64_t size;
	uint64_t offset;
};

static const char *progname = "mkbootarchive";

static void
usage(void)
{
	fprintf(stderr, "usage: %s [-v] -o archive [name=]file ...\n"
	    "       %s -t archive\n", progname, progname);
	exit(1);
}

/*
 * Little-endian stores. The struct is filled through these rather than by
 * assignment so that the output does not depend on the byte order of the
 * machine running the build - a cross build is the normal case here.
 */
static void
put32(void *dst, uint32_t v)
{
	unsigned char *p = dst;

	p[0] = (unsigned char)v;
	p[1] = (unsigned char)(v >> 8);
	p[2] = (unsigned char)(v >> 16);
	p[3] = (unsigned char)(v >> 24);
}

static void
put64(void *dst, uint64_t v)
{
	put32(dst, (uint32_t)v);
	put32((unsigned char *)dst + 4, (uint32_t)(v >> 32));
}

static uint32_t
get32(const void *src)
{
	const unsigned char *p = src;

	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	    ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t
get64(const void *src)
{
	return (uint64_t)get32(src) |
	    ((uint64_t)get32((const unsigned char *)src + 4) << 32);
}

static uint64_t
roundup_align(uint64_t v)
{
	return (v + BOOT_ARCHIVE_ALIGN - 1) & ~(uint64_t)(BOOT_ARCHIVE_ALIGN - 1);
}

/*
 * Split "name=path" or "path" into the two. The name defaults to the last
 * path component, which is what the tree's object directories give.
 */
static void
split_arg(const char *arg, struct image *im)
{
	const char *eq = strchr(arg, '=');
	const char *name;
	size_t len;

	if (eq != NULL) {
		name = arg;
		len = (size_t)(eq - arg);
		im->path = eq + 1;
	} else {
		const char *slash = strrchr(arg, '/');

		im->path = arg;
		name = slash != NULL ? slash + 1 : arg;
		len = strlen(name);
	}

	if (len == 0)
		errx(1, "%s: empty image name", arg);
	if (len > sizeof(im->name))
		errx(1, "%s: name is %zu bytes, the field holds %zu",
		    arg, len, sizeof(im->name));
	if (im->path[0] == '\0')
		errx(1, "%s: empty file name", arg);

	/* NUL-padded, and a name may fill the field with no terminator. */
	memset(im->name, 0, sizeof(im->name));
	memcpy(im->name, name, len);
}

/*
 * Every entry of a boot archive is a boot module, and a boot module is an
 * ELF executable that the kernel hands to libexec. Checking here turns a
 * build accident - a wrapper script, a stripped-to-nothing file, the wrong
 * member of an object directory - into a message from the build instead of
 * "VM loading failed" on a machine with nothing else to say.
 */
static void
check_elf(const struct image *im, FILE *f)
{
	unsigned char id[4];

	if (fread(id, 1, sizeof(id), f) != sizeof(id) ||
	    memcmp(id, "\177ELF", sizeof(id)) != 0)
		errx(1, "%s: not an ELF image", im->path);
	rewind(f);
}

static void
pad_to(FILE *out, uint64_t from, uint64_t to)
{
	static const char zeros[512];

	while (from < to) {
		size_t n = (size_t)(to - from);

		if (n > sizeof(zeros))
			n = sizeof(zeros);
		if (fwrite(zeros, 1, n, out) != n)
			err(1, "writing padding");
		from += n;
	}
}

static void
print_toc(const struct image *im, unsigned count)
{
	char name[BOOT_ARCHIVE_NAMELEN + 1];
	unsigned i;

	printf("%-*s %12s %10s\n", BOOT_ARCHIVE_NAMELEN, "name", "offset",
	    "size");
	for (i = 0; i < count; i++) {
		/* A name may fill the field with no terminator. */
		memcpy(name, im[i].name, BOOT_ARCHIVE_NAMELEN);
		name[BOOT_ARCHIVE_NAMELEN] = '\0';

		printf("%-*s %12llu %10llu\n", BOOT_ARCHIVE_NAMELEN, name,
		    (unsigned long long)im[i].offset,
		    (unsigned long long)im[i].size);
	}
}

static int
build(const char *outpath, int count, char **args, int verbose)
{
	struct boot_archive_header hdr;
	struct boot_archive_entry ent;
	struct image *im;
	uint64_t off;
	FILE *out;
	int i, j;

	if ((im = calloc((size_t)count, sizeof(*im))) == NULL)
		err(1, "calloc");

	/*
	 * The index has to be written before the images, and its size decides
	 * where the first image goes, so every size is collected first.
	 */
	off = roundup_align(sizeof(hdr) +
	    (uint64_t)count * sizeof(ent));

	for (i = 0; i < count; i++) {
		struct stat st;

		split_arg(args[i], &im[i]);

		for (j = 0; j < i; j++)
			if (memcmp(im[j].name, im[i].name,
			    sizeof(im[i].name)) == 0)
				errx(1, "%.*s: named twice",
				    BOOT_ARCHIVE_NAMELEN, im[i].name);

		if (stat(im[i].path, &st) != 0)
			err(1, "%s", im[i].path);
		if (st.st_size <= 0)
			errx(1, "%s: empty", im[i].path);

		im[i].size = (uint64_t)st.st_size;
		im[i].offset = off;
		off += roundup_align(im[i].size);
	}

	if ((out = fopen(outpath, "wb")) == NULL)
		err(1, "%s", outpath);

	memset(&hdr, 0, sizeof(hdr));
	put32(&hdr.magic, BOOT_ARCHIVE_MAGIC);
	put32(&hdr.version, BOOT_ARCHIVE_VERSION);
	put32(&hdr.count, (uint32_t)count);
	put32(&hdr.reserved, 0);
	put64(&hdr.total_size, off);
	if (fwrite(&hdr, sizeof(hdr), 1, out) != 1)
		err(1, "%s", outpath);

	for (i = 0; i < count; i++) {
		memset(&ent, 0, sizeof(ent));
		put64(&ent.offset, im[i].offset);
		put64(&ent.size, im[i].size);
		memcpy(ent.name, im[i].name, sizeof(ent.name));
		if (fwrite(&ent, sizeof(ent), 1, out) != 1)
			err(1, "%s", outpath);
	}

	pad_to(out, sizeof(hdr) + (uint64_t)count * sizeof(ent),
	    im[0].offset);

	for (i = 0; i < count; i++) {
		uint64_t left = im[i].size;
		FILE *in;

		if ((in = fopen(im[i].path, "rb")) == NULL)
			err(1, "%s", im[i].path);
		check_elf(&im[i], in);

		while (left > 0) {
			char buf[65536];
			size_t want = left > sizeof(buf) ?
			    sizeof(buf) : (size_t)left;
			size_t got = fread(buf, 1, want, in);

			/*
			 * The size came from stat() before the file was
			 * opened. A short read means it shrank in between,
			 * which would leave a hole the kernel reads as the
			 * head of an image.
			 */
			if (got == 0)
				errx(1, "%s: shrank while being read",
				    im[i].path);
			if (fwrite(buf, 1, got, out) != got)
				err(1, "%s", outpath);
			left -= got;
		}
		fclose(in);

		pad_to(out, im[i].offset + im[i].size,
		    im[i].offset + roundup_align(im[i].size));
	}

	if (fclose(out) != 0)
		err(1, "%s", outpath);

	if (verbose) {
		print_toc(im, (unsigned)count);
		printf("%s: %d image%s, %llu bytes\n", outpath, count,
		    count == 1 ? "" : "s", (unsigned long long)off);
	}

	free(im);
	return 0;
}

static int
list(const char *path)
{
	struct boot_archive_header hdr;
	struct boot_archive_entry ent;
	struct image *im;
	uint64_t total;
	unsigned count, i;
	FILE *f;

	if ((f = fopen(path, "rb")) == NULL)
		err(1, "%s", path);
	if (fread(&hdr, sizeof(hdr), 1, f) != 1)
		errx(1, "%s: too short to be a boot archive", path);
	if (get32(&hdr.magic) != BOOT_ARCHIVE_MAGIC)
		errx(1, "%s: not a boot archive", path);
	if (get32(&hdr.version) != BOOT_ARCHIVE_VERSION)
		errx(1, "%s: version %u, this tool speaks %u", path,
		    get32(&hdr.version), BOOT_ARCHIVE_VERSION);

	count = get32(&hdr.count);
	total = get64(&hdr.total_size);
	if (count == 0)
		errx(1, "%s: no images", path);

	if ((im = calloc(count, sizeof(*im))) == NULL)
		err(1, "calloc");

	for (i = 0; i < count; i++) {
		if (fread(&ent, sizeof(ent), 1, f) != 1)
			errx(1, "%s: index ends after %u of %u entries",
			    path, i, count);
		im[i].offset = get64(&ent.offset);
		im[i].size = get64(&ent.size);
		memcpy(im[i].name, ent.name, sizeof(im[i].name));

		if (im[i].offset > total || im[i].size > total - im[i].offset)
			errx(1, "%s: entry %u runs past the end", path, i);
	}
	fclose(f);

	print_toc(im, count);
	printf("%s: %u image%s, %llu bytes\n", path, count,
	    count == 1 ? "" : "s", (unsigned long long)total);

	free(im);
	return 0;
}

int
main(int argc, char **argv)
{
	const char *out = NULL;
	int tflag = 0, vflag = 0;
	int c;

	if (argc > 0 && argv[0][0] != '\0')
		progname = argv[0];

	while ((c = getopt(argc, argv, "o:tv")) != -1) {
		switch (c) {
		case 'o':
			out = optarg;
			break;
		case 't':
			tflag = 1;
			break;
		case 'v':
			vflag = 1;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (tflag) {
		if (out != NULL || argc != 1)
			usage();
		return list(argv[0]);
	}

	if (out == NULL || argc < 1)
		usage();

	return build(out, argc, argv, vflag);
}
