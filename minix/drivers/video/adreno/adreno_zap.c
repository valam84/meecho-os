/*
 * The zap shader (port/clover/hw-gpu.md, §5, §15): the GPU comes out of
 * reset in secure mode (SECVID_TRUST_CNTL 1 on the tablet, §13), and the
 * command processor leaves it - CP_SET_SECURE_MODE 0 - only once TrustZone
 * has loaded and checked a shader signed for this device.  Stage zap does
 * the loading, in Linux's order (adreno_gpu.c, zap_shader_load_mdt();
 * mdt_loader.c, qcom_mdt_load()), every TrustZone call by sys_qcomscm(2):
 *
 *   1. the .mdt read and understood (adreno_mdt.c), every segment it names
 *      read too - a missing file stops here, before TrustZone is told
 *      anything;
 *   2. the region the tree reserves for it (the zap-shader node's
 *      memory-region, "no-map", 8 KiB at 0xf0b00000 on the tablet) mapped
 *      and its pages touched: the kernel finds a caller's memory through
 *      its page table, and a mapping of physical memory is filled in on
 *      first touch;
 *   3. PAS_INIT_IMAGE with the metadata - the ELF headers, then the hash
 *      segment - in a page-aligned, physically contiguous buffer;
 *   4. PAS_MEM_SETUP with the region, if the image is relocatable: from
 *      here on the region is TrustZone's to take;
 *   5. every loadable segment copied in at its offset from the lowest one,
 *      the rest of its size zeroed, all of it read back;
 *   6. the mapping dropped, and PAS_AUTH_AND_RESET: TrustZone checks the
 *      image against the hashes and keeps it.
 *
 * The GPU is powered while this runs, as it is under Linux, which loads the
 * shader from its hw_init() after the CP has started: what TrustZone does
 * to the GPU at AUTH_AND_RESET is not known.
 *
 * TrustZone keeps the image across the GPU's power cycles.  Every start
 * after the first therefore only resumes it - SET_REMOTE_STATE(0, 13), as
 * Linux's a5xx_zap_shader_resume() - and never touches the region again:
 * the kernel says which (QCOMSCM_PAS_STATE).  A load that named the region
 * to TrustZone and did not finish leaves it TrustZone's too; then nothing
 * is tried again before a reboot.
 *
 * The answers are lines "Q18 ...".
 */

#include "adreno.h"
#include "adreno_mdt.h"
#include "adreno_regs.h"

#include <minix/qcomscm.h>
#include <minix/vm.h>
#include <sys/mman.h>

#include <stdlib.h>
#include <string.h>

#define ZAP_PAS		QCOMSCM_PAS_GPU
#define MDT_MAX		(256 * 1024)	/* the tablet's: 6684 bytes */
#define SEG_MAX		(1024 * 1024)

static const char *const state_name[] = { "none", "inited", "given",
    "authenticated" };

/* What a PAS call came to, in a word, and whether it succeeded. */
static int
scm_said(int r, const struct qcomscm_answer *a, char *buf, size_t size)
{
	if (r != OK)
		snprintf(buf, size, "call %d", r);
	else if (a->status != 0)
		snprintf(buf, size, "err %ld", a->status);
	else if (a->res[0] != 0)
		snprintf(buf, size, "refused %ld", (long)a->res[0]);
	else
		snprintf(buf, size, "ok");
	return r == OK && a->status == 0 && a->res[0] == 0;
}

/* Bytes into memory mapped as a device: aligned words, single bytes at the
 * ends - an unaligned access to Device memory is an alignment fault. */
static void
dev_put(volatile uint8_t *dst, const uint8_t *src, size_t n)
{
	size_t i = 0;
	uint32_t w;

	while (i < n && ((uintptr_t)(dst + i) & 3) != 0) {
		dst[i] = src != NULL ? src[i] : 0;
		i++;
	}
	for (; i + 4 <= n; i += 4) {
		w = 0;
		if (src != NULL)
			memcpy(&w, src + i, 4);
		*(volatile uint32_t *)(volatile void *)(dst + i) = w;
	}
	for (; i < n; i++)
		dst[i] = src != NULL ? src[i] : 0;
}

static uint32_t
dev_sum(const volatile uint8_t *p, size_t n, uint32_t h)
{
	size_t i;
	uint8_t b;

	for (i = 0; i < n; i++) {
		b = p[i];
		h = fw_sum(&b, 1, h);
	}
	return h;
}

/* A segment's bytes: from the .mdt itself, or from its own .bNN. */
static int
seg_bytes(const struct mdt *m, unsigned i, const uint8_t *file, size_t len,
	void **out)
{
	char name[48];
	size_t n;
	void *b;
	int r;

	*out = NULL;
	if (m->ph[i].filesz == 0)
		return OK;
	if (!m->split) {
		if ((uint64_t)m->ph[i].offset + m->ph[i].filesz > len)
			return EINVAL;
		if ((b = malloc(m->ph[i].filesz)) == NULL)
			return ENOMEM;
		memcpy(b, file + m->ph[i].offset, m->ph[i].filesz);
		*out = b;
		return OK;
	}
	if (mdt_seg_name(ad.dt.zap_fw, i, name, sizeof(name)) != 0)
		return ENAMETOOLONG;
	if ((r = fw_read(name, &b, &n, SEG_MAX)) != OK) {
		log_warn(&ad_log, "Q18 zap: %s/%s: %d; refusing\n", ad_fwdir,
		    name, r);
		return r;
	}
	if (n != m->ph[i].filesz) {
		log_warn(&ad_log, "Q18 zap: %s is %zu bytes, segment %u says %u;"
		    " refusing\n", name, n, i, (unsigned)m->ph[i].filesz);
		free(b);
		return EINVAL;
	}
	*out = b;
	return OK;
}

static int
zap_resume(void)
{
	struct qcomscm_answer a;
	char said[32];
	int r;

	r = sys_qcomscm(QCOMSCM_REMOTE_STATE, QCOMSCM_STATE_RESUME, ZAP_PAS,
	    &a);
	if (scm_said(r, &a, said, sizeof(said))) {
		log_info(&ad_log, "Q18 zap: authenticated since boot; "
		    "set_remote_state(resume, %d): %s; secvid %08x\n", ZAP_PAS,
		    said, ad_rd(&ad.gpu, A5XX(A5XX_RBBM_SECVID_TRUST_CNTL)));
		return OK;
	}
	log_warn(&ad_log, "Q18 zap: set_remote_state(resume, %d): %s\n",
	    ZAP_PAS, said);
	return EIO;
}

int
zap_load(void)
{
	struct qcomscm_answer a;
	struct mdt m;
	const char *why;
	char said[32];
	void *file = NULL, *seg[MDT_MAXPH], *hashb = NULL;
	uint8_t *meta = NULL;
	uint8_t *reg = NULL;		/* the region, mapped as a device */
	phys_bytes meta_pa;
	size_t len, meta_len = 0, meta_alloc = 0, n;
	uint32_t want, got, off, span;
	unsigned i, k, p;
	int r;

	memset(seg, 0, sizeof(seg));
	if (ad.dt.zap_fw[0] == '\0' || ad.dt.zap_sz == 0) {
		log_warn(&ad_log, "Q18 zap: no zap-shader node with firmware-name"
		    " and memory-region in the tree; refusing\n");
		return ENOENT;
	}
	if (!cc_gpu_clocked(&why)) {
		log_warn(&ad_log, "Q18 zap: %s is off; refusing\n", why);
		return EIO;
	}

	/* What this boot has done with peripheral 13 already. */
	if ((r = sys_qcomscm(QCOMSCM_PAS_STATE, 0, ZAP_PAS, &a)) != OK) {
		log_warn(&ad_log, "Q18 zap: sys_qcomscm: %d%s\n", r,
		    r == ENODEV ? " (no qcom,scm in the tree)" : "");
		return r;
	}
	if (a.res[0] == QCOMSCM_PAS_AUTHED)
		return zap_resume();
	if (a.res[0] == QCOMSCM_PAS_GIVEN) {
		log_warn(&ad_log, "Q18 zap: pas %d was given its memory by a "
		    "load that did not finish; the region is TrustZone's until "
		    "a reboot - refusing\n", ZAP_PAS);
		return EBUSY;
	}

	/* 1. The files. */
	if ((r = fw_read(ad.dt.zap_fw, &file, &len, MDT_MAX)) != OK) {
		log_warn(&ad_log, "Q18 zap: %s/%s: %d; refusing\n", ad_fwdir,
		    ad.dt.zap_fw, r);
		return r;
	}
	if (mdt_parse(file, len, &m, &why) != 0) {
		log_warn(&ad_log, "Q18 zap: %s: %s; refusing\n", ad.dt.zap_fw,
		    why);
		r = EINVAL;
		goto out;
	}
	span = m.max_addr - m.min_addr;
	log_info(&ad_log, "Q18 zap: %s %zu bytes, %u headers, hash %u (%u "
	    "bytes%s), load %u%s%s; image %x..%x, %x bytes, state %s\n",
	    ad.dt.zap_fw, len, m.nph, m.hash, (unsigned)m.hash_size,
	    m.hash_in_file ? "" : ", own file", m.nload,
	    m.relocatable ? ", relocatable" : "", m.split ? ", split" : "",
	    (unsigned)m.min_addr, (unsigned)m.max_addr, (unsigned)span,
	    state_name[a.res[0] < 4 ? a.res[0] : 0]);
	for (k = 0; k < m.nload; k++) {
		i = m.load[k];
		off = m.relocatable ? m.ph[i].paddr - m.min_addr :
		    m.ph[i].paddr - (uint32_t)ad.dt.zap_pa;
		if ((!m.relocatable && m.ph[i].paddr < ad.dt.zap_pa) ||
		    (uint64_t)off + m.ph[i].memsz > ad.dt.zap_sz) {
			log_warn(&ad_log, "Q18 zap: segment %u (%x+%x) outside "
			    "the region %llx+%llx; refusing\n", i,
			    (unsigned)m.ph[i].paddr, (unsigned)m.ph[i].memsz,
			    (unsigned long long)ad.dt.zap_pa,
			    (unsigned long long)ad.dt.zap_sz);
			r = EINVAL;
			goto out;
		}
		if ((r = seg_bytes(&m, i, file, len, &seg[i])) != OK)
			goto out;
	}
	if (!m.hash_in_file) {
		char name[48];

		if (mdt_seg_name(ad.dt.zap_fw, m.hash, name, sizeof(name)) != 0 ||
		    (r = fw_read(name, &hashb, &n, SEG_MAX)) != OK ||
		    n != m.hash_size) {
			log_warn(&ad_log, "Q18 zap: the hash segment's file: %d; "
			    "refusing\n", r);
			r = r != OK ? r : EINVAL;
			goto out;
		}
	}

	/* 2. The region, mapped and touched. */
	reg = vm_map_phys(SELF, (void *)(phys_bytes)ad.dt.zap_pa,
	    (size_t)ad.dt.zap_sz);
	if (reg == MAP_FAILED) {
		reg = NULL;
		log_warn(&ad_log, "Q18 zap: cannot map the region %llx+%llx "
		    "(RS grants it by the zap-shader's memory-region); "
		    "refusing\n", (unsigned long long)ad.dt.zap_pa,
		    (unsigned long long)ad.dt.zap_sz);
		r = EPERM;
		goto out;
	}
	for (p = 0; p < ad.dt.zap_sz; p += 4096)
		(void)*(volatile uint32_t *)(volatile void *)(reg + p);

	/* 3. The metadata: the headers, then the hash, nothing between. */
	meta_len = m.ehdr_size + m.hash_size;
	meta_alloc = (meta_len + 4095) & ~(size_t)4095;
	if ((meta = alloc_contig(meta_alloc, AC_ALIGN4K, &meta_pa)) == NULL) {
		log_warn(&ad_log, "Q18 zap: no memory for the metadata\n");
		r = ENOMEM;
		goto out;
	}
	memset(meta, 0, meta_alloc);
	memcpy(meta, file, m.ehdr_size);
	memcpy(meta + m.ehdr_size, m.hash_in_file ? (uint8_t *)file +
	    m.hash_off : (uint8_t *)hashb, m.hash_size);
	r = sys_qcomscm_mem(QCOMSCM_PAS_INIT, ZAP_PAS, meta, meta_len, &a);
	if (!scm_said(r, &a, said, sizeof(said))) {
		log_warn(&ad_log, "Q18 zap: init_image(%d, %zu bytes at %lx): "
		    "%s; stopping\n", ZAP_PAS, meta_len, (unsigned long)meta_pa,
		    said);
		r = EIO;
		goto out;
	}
	log_info(&ad_log, "Q18 zap: init_image(%d, %zu bytes at %lx): %s\n",
	    ZAP_PAS, meta_len, (unsigned long)meta_pa, said);
	free_contig(meta, meta_alloc);
	meta = NULL;

	/* 4. Where the image goes. */
	if (m.relocatable) {
		r = sys_qcomscm_mem(QCOMSCM_PAS_MEM_SETUP, ZAP_PAS, reg,
		    span, &a);
		if (!scm_said(r, &a, said, sizeof(said))) {
			log_warn(&ad_log, "Q18 zap: mem_setup(%d, %llx, %x): %s; "
			    "stopping\n", ZAP_PAS,
			    (unsigned long long)ad.dt.zap_pa, (unsigned)span,
			    said);
			r = EIO;
			goto out;
		}
		log_info(&ad_log, "Q18 zap: mem_setup(%d, %llx, %x): %s\n",
		    ZAP_PAS, (unsigned long long)ad.dt.zap_pa, (unsigned)span,
		    said);
	}

	/* 5. The segments, each read back. */
	for (k = 0; k < m.nload; k++) {
		i = m.load[k];
		off = m.relocatable ? m.ph[i].paddr - m.min_addr :
		    m.ph[i].paddr - (uint32_t)ad.dt.zap_pa;
		dev_put(reg + off, seg[i], m.ph[i].filesz);
		dev_put(reg + off + m.ph[i].filesz, NULL,
		    m.ph[i].memsz - m.ph[i].filesz);
		want = fw_sum(seg[i], m.ph[i].filesz, FW_SUM0);
		for (n = m.ph[i].filesz; n < m.ph[i].memsz; n++) {
			uint8_t z = 0;

			want = fw_sum(&z, 1, want);
		}
		got = dev_sum(reg + off, m.ph[i].memsz, FW_SUM0);
		log_info(&ad_log, "Q18 zap: segment %u at +%x: %u bytes + %u "
		    "zero, read back %s\n", i, (unsigned)off,
		    (unsigned)m.ph[i].filesz,
		    (unsigned)(m.ph[i].memsz - m.ph[i].filesz),
		    got == want ? "the same" : "DIFFERENT");
		if (got != want) {
			r = EIO;
			goto out;
		}
	}

	/* 6. Ours no more: unmapped, then checked and kept by TrustZone. */
	(void)vm_unmap_phys(SELF, reg, (size_t)ad.dt.zap_sz);
	reg = NULL;
	r = sys_qcomscm(QCOMSCM_PAS_AUTH, 0, ZAP_PAS, &a);
	if (!scm_said(r, &a, said, sizeof(said))) {
		log_warn(&ad_log, "Q18 zap: auth_and_reset(%d): %s\n", ZAP_PAS,
		    said);
		r = EIO;
		goto out;
	}
	log_info(&ad_log, "Q18 zap: auth_and_reset(%d): %s - authenticated; "
	    "secvid %08x\n", ZAP_PAS, said,
	    ad_rd(&ad.gpu, A5XX(A5XX_RBBM_SECVID_TRUST_CNTL)));
	r = OK;
out:
	if (reg != NULL)
		(void)vm_unmap_phys(SELF, reg, (size_t)ad.dt.zap_sz);
	if (meta != NULL)
		free_contig(meta, meta_alloc);
	for (i = 0; i < MDT_MAXPH; i++)
		free(seg[i]);
	free(hashb);
	free(file);
	return r;
}
