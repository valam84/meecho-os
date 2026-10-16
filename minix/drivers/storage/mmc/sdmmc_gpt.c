/*
 * One partition of a GPT disk, as the whole of what this driver serves.
 *
 * A phone's eMMC is not a disk an operating system owns. The Mi Pad 4's
 * holds some sixty partitions behind a GUID partition table - the boot
 * loaders (xbl, abl), TrustZone, the modem's firmware, calibration data that
 * exists nowhere else - and one of them, userdata, is what Android used for
 * its files and what MEECHO may have. A write anywhere else can leave the
 * tablet unable to start, beyond what fastboot can repair.
 *
 * So with gptpart=NAME the driver reads the GPT, finds the partition by name
 * and from then on that partition is the device: minor 0 starts at its first
 * sector and ends at its last, and sdmmc_transfer() refuses anything outside
 * it whatever the partition code above it believes. The table is checked as
 * the specification says before anything is trusted - signature, header CRC,
 * the header's own LBA, the entries' CRC, sizes, the partition within the
 * usable range - and any doubt is a refusal to serve at all, never a guess:
 * a driver that falls back to the whole disk when the table looks odd is the
 * driver that writes over a boot loader.
 *
 * UEFI 2.10, section 5.3 (GUID Partition Table disk layout).
 */

#include <minix/drivers.h>
#include <minix/log.h>

#include <stdint.h>
#include <string.h>

#include "sdmmc.h"

#define GPT_HEADER_LBA		1
#define GPT_SIGNATURE		"EFI PART"
#define GPT_MIN_HEADER		92
#define GPT_MIN_ENTRY		128
#define GPT_NAME_UNITS		36	/* UTF-16 code units in an entry */
#define GPT_MAX_ENTRIES		1024	/* far more than any eMMC has */

static uint32_t
le32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
	    (uint32_t)p[3] << 24;
}

static uint64_t
le64(const uint8_t *p)
{
	return (uint64_t)le32(p) | (uint64_t)le32(p + 4) << 32;
}

/* CRC-32 as the GPT uses it: IEEE 802.3, reflected, 0xedb88320. */
static uint32_t
crc32_update(uint32_t crc, const uint8_t *p, size_t n)
{
	int k;

	crc = ~crc;
	while (n-- > 0) {
		crc ^= *p++;
		for (k = 0; k < 8; k++)
			crc = (crc >> 1) ^ (0xedb88320u & -(crc & 1));
	}
	return ~crc;
}

/* Whether a UTF-16LE name of GPT_NAME_UNITS units is the ASCII string s. */
static int
name_is(const uint8_t *utf16, const char *s)
{
	size_t i, n = strlen(s);

	if (n == 0 || n > GPT_NAME_UNITS)
		return 0;
	for (i = 0; i < n; i++)
		if (utf16[2 * i] != (uint8_t)s[i] || utf16[2 * i + 1] != 0)
			return 0;
	return n == GPT_NAME_UNITS ||
	    (utf16[2 * n] == 0 && utf16[2 * n + 1] == 0);
}

/*
 * Find partition "name" on a card of "sectors" sectors.  "buf" is the
 * driver's transfer buffer, "bytes" long at physical "phys", and is used to
 * read through.  On success the partition's first sector and its length in
 * sectors; on any doubt about the table, an error and nothing.
 *
 * With name NULL nothing is looked up: the table is checked all the same,
 * and the number of partitions and the largest one are reported - what a
 * person about to give one of them to gptpartN= wants to see first, on the
 * screen of the machine itself.  first and count are then the largest's.
 */
int
sdmmc_gpt_find(const char *name, uint64_t sectors, uint8_t *buf, size_t bytes,
	phys_bytes phys, uint64_t *first, uint64_t *count)
{
	uint8_t hdr[SDMMC_SECTOR_SIZE];
	uint32_t hsize, hcrc, nent, esize, ecrc, crc;
	uint64_t my_lba, first_usable, last_usable, ent_lba, lo, hi;
	uint64_t table_bytes, done, lba;
	unsigned per_read, i, found = 0, used = 0;
	char big[GPT_NAME_UNITS + 1];
	int r;

	big[0] = '\0';

	if (bytes < SDMMC_SECTOR_SIZE)
		return EINVAL;

	/* The header, and everything it says about itself. */
	if ((r = sdmmc_card_read(GPT_HEADER_LBA, 1, buf, phys)) != OK) {
		log_warn(&sdmmc_log, "gpt: cannot read the header: %d\n", r);
		return r;
	}
	memcpy(hdr, buf, sizeof(hdr));
	if (memcmp(hdr, GPT_SIGNATURE, 8) != 0) {
		log_warn(&sdmmc_log, "gpt: no GPT signature at LBA 1\n");
		return ENXIO;
	}
	hsize = le32(hdr + 12);
	hcrc = le32(hdr + 16);
	if (hsize < GPT_MIN_HEADER || hsize > SDMMC_SECTOR_SIZE) {
		log_warn(&sdmmc_log, "gpt: header size %u\n", hsize);
		return ENXIO;
	}
	memset(hdr + 16, 0, 4);			/* the CRC covers itself as 0 */
	if ((crc = crc32_update(0, hdr, hsize)) != hcrc) {
		log_warn(&sdmmc_log, "gpt: header CRC 0x%08x, computed 0x%08x\n",
		    hcrc, crc);
		return ENXIO;
	}
	my_lba = le64(hdr + 24);
	first_usable = le64(hdr + 40);
	last_usable = le64(hdr + 48);
	ent_lba = le64(hdr + 72);
	nent = le32(hdr + 80);
	esize = le32(hdr + 84);
	ecrc = le32(hdr + 88);
	if (my_lba != GPT_HEADER_LBA || first_usable > last_usable ||
	    last_usable >= sectors || esize < GPT_MIN_ENTRY ||
	    esize % 8 != 0 || SDMMC_SECTOR_SIZE % esize != 0 ||
	    nent == 0 || nent > GPT_MAX_ENTRIES || ent_lba < 2 ||
	    ent_lba >= first_usable) {
		log_warn(&sdmmc_log, "gpt: header does not add up (lba %llu, "
		    "usable %llu-%llu of %llu, %u entries of %u at %llu)\n",
		    (unsigned long long)my_lba,
		    (unsigned long long)first_usable,
		    (unsigned long long)last_usable,
		    (unsigned long long)sectors, nent, esize,
		    (unsigned long long)ent_lba);
		return ENXIO;
	}

	/* The entries: CRC over the whole array, then the name looked up. */
	table_bytes = (uint64_t)nent * esize;
	per_read = (unsigned)(bytes / SDMMC_SECTOR_SIZE);
	crc = 0;
	*first = *count = 0;
	for (done = 0, lba = ent_lba; done < table_bytes; lba += per_read) {
		uint64_t left = table_bytes - done;
		unsigned n = per_read, take;

		if ((uint64_t)n * SDMMC_SECTOR_SIZE > left)
			n = (unsigned)((left + SDMMC_SECTOR_SIZE - 1) /
			    SDMMC_SECTOR_SIZE);
		if ((r = sdmmc_card_read(lba, n, buf, phys)) != OK) {
			log_warn(&sdmmc_log, "gpt: cannot read entries at "
			    "%llu: %d\n", (unsigned long long)lba, r);
			return r;
		}
		take = (unsigned)((uint64_t)n * SDMMC_SECTOR_SIZE < left ?
		    (uint64_t)n * SDMMC_SECTOR_SIZE : left);
		crc = crc32_update(crc, buf, take);
		for (i = 0; i + esize <= take; i += esize) {
			const uint8_t *e = buf + i;
			static const uint8_t zero[16];

			if (memcmp(e, zero, 16) == 0)	/* unused entry */
				continue;
			lo = le64(e + 32);
			hi = le64(e + 40);
			if (name == NULL) {
				unsigned k;

				used++;
				if (hi < lo || hi - lo + 1 <= *count)
					continue;
				*first = lo;
				*count = hi - lo + 1;
				for (k = 0; k < GPT_NAME_UNITS; k++) {
					uint8_t c = e[56 + 2 * k];

					if (c == 0 && e[57 + 2 * k] == 0)
						break;
					big[k] = (c >= 0x20 && c < 0x7f &&
					    e[57 + 2 * k] == 0) ? (char)c :
					    '?';
				}
				big[k] = '\0';
				continue;
			}
			if (!name_is(e + 56, name))
				continue;
			if (found++ == 0) {
				*first = lo;
				*count = hi >= lo ? hi - lo + 1 : 0;
			}
		}
		done += take;
	}
	if (crc != ecrc) {
		log_warn(&sdmmc_log, "gpt: entries CRC 0x%08x, computed "
		    "0x%08x\n", ecrc, crc);
		return ENXIO;
	}
	if (name == NULL) {
		log_info(&sdmmc_log, "gpt: table good, %u partitions; the "
		    "largest is \"%s\", sectors %llu-%llu (%llu MiB)\n", used,
		    big, (unsigned long long)*first,
		    (unsigned long long)(*first + *count - 1),
		    (unsigned long long)(*count / 2048));
		return OK;
	}
	if (found != 1) {
		log_warn(&sdmmc_log, "gpt: %u partitions named \"%s\"; "
		    "want exactly one\n", found, name);
		return ENOENT;
	}
	if (*count == 0 || *first < first_usable ||
	    *first + *count - 1 > last_usable) {
		log_warn(&sdmmc_log, "gpt: \"%s\" at %llu, %llu sectors: "
		    "outside the usable range\n", name,
		    (unsigned long long)*first, (unsigned long long)*count);
		return ENXIO;
	}
	log_info(&sdmmc_log, "gpt: \"%s\" is sectors %llu-%llu (%llu MiB); "
	    "nothing outside it is reachable\n", name,
	    (unsigned long long)*first,
	    (unsigned long long)(*first + *count - 1),
	    (unsigned long long)(*count / 2048));
	return OK;
}
