/*
 * qcom_rmtfs_mem: the memory a Qualcomm modem shares with rmtfs, as
 * /dev/qcom_rmtfs_mem<client-id> (<minix/rmtfs_mem.h>) - Linux's
 * rmtfs_mem.c (what it does and in what order, not its code).
 *
 * The tree names it: a "qcom,rmtfs-mem" child of /reserved-memory, no-map,
 * with qcom,client-id (1: the modem) and qcom,vmid (15: the modem's).  At
 * start the range is mapped and given, through TrustZone (MP/ASSIGN), to
 * this side and to every VM the tree names, read and write - without that
 * the modem's first access to it is a fault.  Then the service stays and
 * serves the range: read(2) and write(2) at an offset, word by word (it is
 * mapped as a device), and RMTFS_MEM_INFO for its physical address and
 * size, which rmtfs hands the modem.  port/clover/hw-modem.md.
 */

#include <minix/drivers.h>
#include <minix/chardriver.h>
#include <minix/fdt.h>
#include <minix/log.h>
#include <minix/qcomscm.h>
#include <minix/rmtfs_mem.h>
#include <minix/vm.h>

#include <sys/mman.h>

#include <stdlib.h>
#include <string.h>

#define NR_VMIDS	3
#define CHUNK		4096

static struct log rlog = {
	.name = "rmtfs_mem",
	.log_level = LEVEL_INFO,
	.log_func = default_log
};

static struct {
	int found;
	u64_t pa, sz;
	uint32_t client_id;
	uint32_t vmid[NR_VMIDS];
	unsigned nvmid;
	int guard;
} dt;

static volatile uint8_t *mem;
static uint8_t bounce[CHUNK];

static uint32_t
prop_u32(const struct fdt_node *n, const char *name, unsigned i, uint32_t def)
{
	const uint8_t *p;
	unsigned len;

	if ((p = fdt_getprop(n, name, &len)) == NULL || len < 4 * (i + 1))
		return def;
	return (uint32_t)fdt_read_cells(p + 4 * i, 1);
}

static int
find_cb(void *cookie, int depth, const char *name, const struct fdt_node *n)
{
	unsigned len, i;

	(void)cookie; (void)depth; (void)name;
	if (dt.found || !fdt_node_is_compatible(n, "qcom,rmtfs-mem"))
		return 0;
	if (fdt_node_reg(n, 0, &dt.pa, &dt.sz) != 0)
		return 0;
	dt.found = 1;
	dt.client_id = prop_u32(n, "qcom,client-id", 0, 1);
	if (fdt_getprop(n, "qcom,vmid", &len) != NULL)
		for (i = 0; i < len / 4 && i < NR_VMIDS; i++)
			dt.vmid[dt.nvmid++] = prop_u32(n, "qcom,vmid", i, 0);
	dt.guard = fdt_getprop(n, "qcom,use-guard-pages", &len) != NULL;
	return 1;
}

/* Word copies to and from memory mapped as a device. */
static void
dev_in(size_t off, uint8_t *buf, size_t n)
{
	size_t i = 0;
	uint32_t w;

	for (; i < n && ((off + i) & 3) != 0; i++)
		buf[i] = mem[off + i];
	for (; i + 4 <= n; i += 4) {
		w = *(volatile uint32_t *)(volatile void *)(mem + off + i);
		memcpy(buf + i, &w, 4);
	}
	for (; i < n; i++)
		buf[i] = mem[off + i];
}

static void
dev_out(size_t off, const uint8_t *buf, size_t n)
{
	size_t i = 0;
	uint32_t w;

	for (; i < n && ((off + i) & 3) != 0; i++)
		mem[off + i] = buf[i];
	for (; i + 4 <= n; i += 4) {
		memcpy(&w, buf + i, 4);
		*(volatile uint32_t *)(volatile void *)(mem + off + i) = w;
	}
	for (; i < n; i++)
		mem[off + i] = buf[i];
}

static ssize_t
transfer(devminor_t minor, u64_t pos, endpoint_t endpt, cp_grant_id_t grant,
	size_t size, int write)
{
	size_t done = 0, n;
	int r;

	if ((uint32_t)minor != dt.client_id)
		return ENXIO;
	if (pos >= dt.sz)
		return 0;
	if (size > dt.sz - pos)
		size = (size_t)(dt.sz - pos);
	while (done < size) {
		n = size - done < CHUNK ? size - done : CHUNK;
		if (write) {
			if ((r = sys_safecopyfrom(endpt, grant, done,
			    (vir_bytes)bounce, n)) != OK)
				return r;
			dev_out((size_t)pos + done, bounce, n);
		} else {
			dev_in((size_t)pos + done, bounce, n);
			if ((r = sys_safecopyto(endpt, grant, done,
			    (vir_bytes)bounce, n)) != OK)
				return r;
		}
		done += n;
	}
	return (ssize_t)done;
}

static int
rm_open(devminor_t minor, int access __unused, endpoint_t user __unused)
{
	return (uint32_t)minor == dt.client_id ? OK : ENXIO;
}

static int
rm_close(devminor_t minor __unused)
{
	return OK;
}

static ssize_t
rm_read(devminor_t minor, u64_t pos, endpoint_t endpt, cp_grant_id_t grant,
	size_t size, int flags __unused, cdev_id_t id __unused)
{
	return transfer(minor, pos, endpt, grant, size, 0);
}

static ssize_t
rm_write(devminor_t minor, u64_t pos, endpoint_t endpt, cp_grant_id_t grant,
	size_t size, int flags __unused, cdev_id_t id __unused)
{
	return transfer(minor, pos, endpt, grant, size, 1);
}

static int
rm_ioctl(devminor_t minor, unsigned long request, endpoint_t endpt,
	cp_grant_id_t grant, int flags __unused, endpoint_t user __unused,
	cdev_id_t id __unused)
{
	struct rmtfs_mem_info info;

	if ((uint32_t)minor != dt.client_id)
		return ENXIO;
	if (request != RMTFS_MEM_INFO)
		return ENOTTY;
	memset(&info, 0, sizeof(info));
	info.phys = dt.pa;
	info.size = dt.sz;
	info.client_id = dt.client_id;
	return sys_safecopyto(endpt, grant, 0, (vir_bytes)&info, sizeof(info));
}

static struct chardriver rm_tab = {
	.cdr_open	= rm_open,
	.cdr_close	= rm_close,
	.cdr_read	= rm_read,
	.cdr_write	= rm_write,
	.cdr_ioctl	= rm_ioctl,
};

static int
rm_init(int type __unused, sef_init_info_t *info __unused)
{
	struct qcomscm_answer a;
	uint32_t dst;
	size_t o;
	void *dtb;
	unsigned i;
	long lv;
	int r;

	if (env_parse("log", "d", 0, &lv, LEVEL_NONE, LEVEL_TRACE) == EP_SET)
		rlog.log_level = (int)lv;
	if ((dtb = fdt_fetch()) == NULL)
		return ENXIO;
	(void)fdt_walk(dtb, find_cb, NULL);
	free(dtb);
	if (!dt.found) {
		log_info(&rlog, "no qcom,rmtfs-mem in the tree\n");
		return ENXIO;
	}
	if (dt.guard) {
		dt.pa += 4096;
		dt.sz -= 2 * 4096;
	}
	if ((mem = vm_map_phys(SELF, (void *)(phys_bytes)dt.pa,
	    (size_t)dt.sz)) == MAP_FAILED) {
		log_warn(&rlog, "cannot map %llx+%llx; not granted by RS?\n",
		    (unsigned long long)dt.pa, (unsigned long long)dt.sz);
		return EPERM;
	}
	for (o = 0; o < dt.sz; o += 4096)
		(void)*(volatile uint32_t *)(volatile void *)(mem + o);

	if (dt.nvmid > 0) {
		dst = QCOMSCM_DST(QCOMSCM_VMID_HLOS, QCOMSCM_PERM_RW);
		for (i = 0; i < dt.nvmid; i++)
			dst |= QCOMSCM_DST(dt.vmid[i], QCOMSCM_PERM_RW) <<
			    (8 * (i + 1));
		r = sys_qcomscm_assign((void *)(uintptr_t)mem, (size_t)dt.sz,
		    1u << QCOMSCM_VMID_HLOS, dst, &a);
		if (r != OK || a.status != 0 || a.res[0] != 0) {
			log_warn(&rlog, "assigning it to VM %u: call %d, err "
			    "%ld, %lx\n", dt.vmid[0], r, (long)a.status,
			    (unsigned long)a.res[0]);
			return EIO;
		}
	}
	log_info(&rlog, "%llx+%llx, client %u, shared with VM %u%s\n",
	    (unsigned long long)dt.pa, (unsigned long long)dt.sz, dt.client_id,
	    dt.nvmid ? dt.vmid[0] : 0, dt.nvmid ? "" : " (none named)");
	chardriver_announce();
	return OK;
}

int
main(int argc, char **argv)
{
	env_setargs(argc, argv);
	sef_setcb_init_fresh(rm_init);
	sef_startup();
	chardriver_task(&rm_tab);
	return 0;
}
