/*
 * A Qualcomm MDT image, read (adreno_mdt.h).  Facts from Linux's
 * drivers/soc/qcom/mdt_loader.c (GPL; what is checked and in what order,
 * not the code) - port/clover/hw-gpu.md, §5:
 *
 *   - an ELF32 file whose program headers are 32 bytes and lie inside it
 *     (mdt_header_valid());
 *   - program header 0 is not loadable: its size is that of the headers,
 *     and the metadata starts with that many bytes of the file;
 *   - the hash segment is the first header after 0 of segment type 2; the
 *     metadata goes on with it - right after the headers when the file is
 *     exactly the two (a "split" image: the .mdt is headers and hash), at
 *     its own offset when that lies inside the file, else from its .bNN
 *     (qcom_mdt_read_metadata());
 *   - the loadable segments are PT_LOAD, not the hash, of a size not 0;
 *     the image spans the lowest paddr to the highest end, rounded up to
 *     4 KiB, and is relocatable when any of them says so
 *     (qcom_mdt_get_size(), __qcom_mdt_pas_init());
 *   - an image is split when any segment with bytes ends past the .mdt:
 *     then every loadable segment is read from its own .bNN
 *     (qcom_mdt_bins_are_split()).
 *
 * Linux reads only the ELF32 layout; so does this, and says so of a file
 * that is not little-endian ELF32 rather than misread it.
 */

#include "adreno_mdt.h"

#include <stdio.h>
#include <string.h>

#define EHDR_SIZE	52
#define PHDR_SIZE	32
#define SHDR_SIZE	40
#define PT_LOAD		1

static uint32_t
le32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
	    (uint32_t)p[3] << 24;
}

static uint32_t
le16(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8;
}

int
mdt_parse(const void *file, size_t len, struct mdt *m, const char **why)
{
	const uint8_t *f = file, *ph;
	uint64_t phoff, phnum, shoff, shnum, end;
	unsigned i;

	memset(m, 0, sizeof(*m));
	if (len < EHDR_SIZE || memcmp(f, "\177ELF", 4) != 0) {
		*why = "not an ELF file";
		return -1;
	}
	if (f[4] != 1 || f[5] != 1) {
		*why = "not little-endian ELF32";
		return -1;
	}
	phoff = le32(f + 28);
	phnum = le16(f + 44);
	shoff = le32(f + 32);
	shnum = le16(f + 48);
	if (le16(f + 42) != PHDR_SIZE || phoff + PHDR_SIZE * phnum > len) {
		*why = "program headers not inside the file";
		return -1;
	}
	if ((le16(f + 46) != 0 || shnum != 0) && (le16(f + 46) != SHDR_SIZE ||
	    shoff + SHDR_SIZE * shnum > len)) {
		*why = "section headers not inside the file";
		return -1;
	}
	if (phnum < 2 || phnum > MDT_MAXPH) {
		*why = phnum < 2 ? "fewer than two program headers" :
		    "too many program headers";
		return -1;
	}
	m->nph = (unsigned)phnum;
	for (i = 0; i < m->nph; i++) {
		ph = f + phoff + PHDR_SIZE * i;
		m->ph[i].type = le32(ph + 0);
		m->ph[i].offset = le32(ph + 4);
		m->ph[i].paddr = le32(ph + 12);
		m->ph[i].filesz = le32(ph + 16);
		m->ph[i].memsz = le32(ph + 20);
		m->ph[i].flags = le32(ph + 24);
	}

	/* The metadata. */
	if (m->ph[0].type == PT_LOAD) {
		*why = "program header 0 is loadable";
		return -1;
	}
	for (i = 1; i < m->nph; i++)
		if (MDT_TYPE(m->ph[i].flags) == MDT_TYPE_HASH)
			break;
	if (i == m->nph) {
		*why = "no hash segment";
		return -1;
	}
	m->hash = i;
	m->ehdr_size = m->ph[0].filesz;
	m->hash_size = m->ph[i].filesz;
	if (m->ehdr_size > len || m->ehdr_size < phoff + PHDR_SIZE * phnum) {
		*why = "the headers' size is not that of the headers";
		return -1;
	}
	if ((uint64_t)m->ehdr_size + m->hash_size == len) {
		m->hash_in_file = 1;
		m->hash_off = m->ehdr_size;
	} else if ((uint64_t)m->ph[i].offset + m->hash_size <= len) {
		m->hash_in_file = 1;
		m->hash_off = m->ph[i].offset;
	}

	/* The image. */
	m->min_addr = UINT32_MAX;
	for (i = 0; i < m->nph; i++) {
		const struct mdt_ph *p = &m->ph[i];

		if (p->filesz != 0 && (uint64_t)p->offset + p->filesz > len)
			m->split = 1;
		if (p->type != PT_LOAD || MDT_TYPE(p->flags) == MDT_TYPE_HASH ||
		    p->memsz == 0)
			continue;
		if (p->filesz > p->memsz) {
			*why = "a segment with more bytes in the file than in "
			    "memory";
			return -1;
		}
		end = ((uint64_t)p->paddr + p->memsz + 0xfff) & ~0xfffULL;
		if (end > UINT32_MAX) {
			*why = "a segment past 4 GiB";
			return -1;
		}
		m->load[m->nload++] = i;
		if (p->flags & MDT_RELOCATABLE)
			m->relocatable = 1;
		if (p->paddr < m->min_addr)
			m->min_addr = p->paddr;
		if (end > m->max_addr)
			m->max_addr = (uint32_t)end;
	}
	if (m->nload == 0) {
		*why = "nothing to load";
		return -1;
	}
	*why = NULL;
	return 0;
}

int
mdt_seg_name(const char *mdt_name, unsigned n, char *out, size_t size)
{
	size_t l = strlen(mdt_name);

	if (l < 4 || l + 1 > size || n > 99)
		return -1;
	memcpy(out, mdt_name, l - 3);
	snprintf(out + l - 3, size - (l - 3), "b%02u", n);
	return 0;
}
