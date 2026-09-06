/*
 * The first C the kernel runs, and the boot path across the MMU switch.
 *
 * head.S has put the CPU at EL1 with a stack and a zeroed BSS, and left the
 * physical address of the device tree in the first argument. From here to
 * kmain() the kernel has to find out what machine it is on, build its
 * translation tables, turn the MMU on and move into the upper half.
 *
 * pre_init() does not return. It ends by branching to its own upper-half
 * alias, and calls kmain() from there. The two 32-bit ports return into
 * head.S and change stacks in assembly; they can, because one page table
 * holds both halves for them. Here the kernel half is TTBR1 and the return
 * address in lr is physical, so returning would mean keeping the identity map
 * alive across a stack change for no gain. This is recorded in head.S as
 * well: it is a decision, not a detail.
 *
 * There is also no second, unpaged copy of this file. earm and i386 link one
 * with its symbols prefixed __k_unpaged_, because their early code runs
 * against physical addresses that the linker resolved to virtual ones. This
 * code is PC-relative and turns the MMU on itself, so one copy is enough -
 * see arch/aarch64/Makefile.inc.
 */

#include "kernel/kernel.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <minix/board.h>
#include <minix/com.h>
#include <minix/const.h>
#include <minix/type.h>

#include <machine/bootarchive.h>
#include <machine/memory.h>
#include <machine/vm.h>

#include "archconst.h"
#include "arch_proto.h"
#include "trap.h"
#include "bsp_serial.h"
#include "bsp_intr.h"
#include <minix/fdt.h>

/* Section boundaries from kernel.lds; physical here, the MMU being off. */
extern char __kernel_start[], __kernel_end[];

/*
 * The device tree, kept for the rest of the kernel.
 *
 * Unlike the multiboot structure on the 32-bit ports, the blob does not have
 * to be copied anywhere: it sits in RAM, get_parameters() cuts its range out
 * of the free memory list so nothing allocates over it, and after the switch
 * it is reachable at phys2vir(boot_dtb). The device addresses, the core
 * clock and the CPU count all come out of it later.
 */
phys_bytes boot_dtb;

/*
 * The boot information as pre_init() assembles it, separate from the global
 * kinfo that kmain() copies it into. It is separate on purpose: the global
 * lives in the usermapped section and is what the rest of the system reads,
 * and having kmain() copy into it - rather than find it already filled -
 * keeps the generic boot path the same on every architecture.
 */
static kinfo_t boot_kinfo;

/* Where the boot loader left the initrd, if it left one. See below. */
static phys_bytes initrd_start, initrd_end;

static void pre_init_high(void);

/*===========================================================================*
 *				set_param				     *
 *===========================================================================*/
/*
 * Put name=value into the parameter buffer, which is a run of NUL-terminated
 * strings closed by an empty one. An existing entry with the same name is
 * removed first, so that the last setting of a variable wins - the kernel's
 * env_get() returns the first match.
 */
static void
set_param(kinfo_t *cbi, const char *name, const char *value)
{
	char *p = cbi->param_buf;
	char *bufend = cbi->param_buf + MULTIBOOT_PARAM_BUF_SIZE;
	size_t namelen = strlen(name);
	size_t valuelen = strlen(value);

	/* Variables this level acts on itself. */
	if (!strcmp(name, SERVARNAME))
		cbi->do_serial_debug = 1;
	if (!strcmp(name, SERBAUDVARNAME))
		cbi->serial_debug_baud = atoi(value);

	while (*p) {
		if (!strncmp(p, name, namelen) && p[namelen] == '=') {
			char *q = p;

			while (*q) q++;		/* end of this entry */
			for (q++; q < bufend; q++, p++)
				*p = *q;
			break;
		}
		while (*p++)
			;
		p++;
	}

	/* First free spot: two NULs in a row, or the start of an empty buffer. */
	for (p = cbi->param_buf; p < bufend && (*p || *(p + 1)); p++)
		;
	if (p > cbi->param_buf)
		p++;

	if (p + namelen + valuelen + 3 > bufend) {
		printf("boot parameter buffer full, dropping %s\n", name);
		return;
	}

	strcpy(p, name);
	p[namelen] = '=';
	strcpy(p + namelen + 1, value);
	p[namelen + valuelen + 1] = '\0';
	p[namelen + valuelen + 2] = '\0';
}

/*===========================================================================*
 *				parse_bootargs				     *
 *===========================================================================*/
/*
 * Split /chosen/bootargs into name=value pairs. The property is one string of
 * space-separated assignments, the same shape the boot monitor hands the
 * 32-bit ports.
 */
static void
parse_bootargs(kinfo_t *cbi, const char *args, unsigned len)
{
	static char var[128], value[256];
	unsigned i = 0;

	while (i < len && args[i] != '\0') {
		unsigned v = 0;

		while (i < len && args[i] == ' ')
			i++;
		if (i >= len || args[i] == '\0')
			break;

		while (i < len && args[i] != '\0' && args[i] != '=' &&
		    args[i] != ' ' && v < sizeof(var) - 1)
			var[v++] = args[i++];
		var[v] = '\0';

		if (i >= len || args[i] != '=') {
			/* Not an assignment; skip to the next word. */
			while (i < len && args[i] != '\0' && args[i] != ' ')
				i++;
			continue;
		}
		i++;

		v = 0;
		while (i < len && args[i] != '\0' && args[i] != ' ' &&
		    v < sizeof(value) - 1)
			value[v++] = args[i++];
		value[v] = '\0';

		set_param(cbi, var, value);
	}
}

/*===========================================================================*
 *				the device tree scan			     *
 *===========================================================================*/
struct dtb_scan {
	kinfo_t *cbi;
	unsigned addr_cells;	/* how the root describes an address... */
	unsigned size_cells;	/* ...and a length */
	const char *parent;	/* name of the depth-1 node being walked */
	unsigned ncpu;
};

/* A string property, compared against a literal. */
static int
prop_is(const struct fdt_node *node, const char *prop, const char *want)
{
	unsigned len;
	const char *v = fdt_getprop(node, prop, &len);

	return v != NULL && len > 0 && v[len - 1] == '\0' &&
	    strcmp(v, want) == 0;
}

static void
scan_memory(struct dtb_scan *s, const struct fdt_node *node)
{
	const char *reg;
	unsigned len, off, step;

	if ((reg = fdt_getprop(node, "reg", &len)) == NULL)
		return;

	step = 4 * (s->addr_cells + s->size_cells);
	if (step == 0)
		return;

	for (off = 0; off + step <= len; off += step) {
		u64_t base = fdt_read_cells(reg + off, s->addr_cells);
		u64_t size = fdt_read_cells(reg + off + 4 * s->addr_cells,
		    s->size_cells);

		/*
		 * Two lists, and they are not the same list. pg_add_ram()
		 * takes all of RAM, because the linear map has to cover the
		 * kernel image and the boot modules too; add_memmap() takes
		 * the same range as free memory, and what is already spoken
		 * for is cut out of it afterwards.
		 */
		pg_add_ram((phys_bytes)base, (phys_bytes)size);
		add_memmap(s->cbi, base, size);
	}
}

static void
scan_chosen(struct dtb_scan *s, const struct fdt_node *node)
{
	const void *p;
	unsigned len;

	if ((p = fdt_getprop(node, "bootargs", &len)) != NULL)
		parse_bootargs(s->cbi, p, len);

	/*
	 * The one range the device tree can name. It is not a module list -
	 * MINIX needs NR_BOOT_MODULES separate images and the tree has no way
	 * to describe them - so what the loader leaves here is a boot archive
	 * holding all of them, and load_boot_archive() takes it apart. The
	 * range itself is cut out of free memory either way.
	 */
	if ((p = fdt_getprop(node, "linux,initrd-start", &len)) != NULL &&
	    (len == 4 || len == 8))
		initrd_start = (phys_bytes)fdt_read_cells(p, len / 4);
	if ((p = fdt_getprop(node, "linux,initrd-end", &len)) != NULL &&
	    (len == 4 || len == 8))
		initrd_end = (phys_bytes)fdt_read_cells(p, len / 4);
}

static int
scan_node(void *cookie, int depth, const char *name,
	const struct fdt_node *node)
{
	struct dtb_scan *s = cookie;
	const void *p;
	unsigned len;

	if (depth == 0) {
		/*
		 * How wide an address and a length are in a child's reg
		 * property. The specification's defaults are 2 and 1; every
		 * AArch64 tree in practice says 2 and 2, and says so
		 * explicitly, but the defaults are what a silent tree means.
		 */
		s->addr_cells = 2;
		s->size_cells = 1;
		if ((p = fdt_getprop(node, "#address-cells", &len)) != NULL &&
		    len == 4)
			s->addr_cells = (unsigned)fdt_read_cells(p, 1);
		if ((p = fdt_getprop(node, "#size-cells", &len)) != NULL &&
		    len == 4)
			s->size_cells = (unsigned)fdt_read_cells(p, 1);
		return 0;
	}

	if (depth == 1) {
		s->parent = name;

		if (prop_is(node, "device_type", "memory"))
			scan_memory(s, node);
		else if (!strcmp(name, "chosen"))
			scan_chosen(s, node);
		return 0;
	}

	/*
	 * Cores, counted for the CPU that will need them. The walk reaches
	 * parents before children, so the depth-1 name recorded above is this
	 * node's parent.
	 */
	if (depth == 2 && s->parent != NULL && !strcmp(s->parent, "cpus") &&
	    prop_is(node, "device_type", "cpu"))
		s->ncpu++;

	return 0;
}

/*===========================================================================*
 *				the boot archive			     *
 *===========================================================================*/
/*
 * An archive name against a boot image name. The field is fixed width and
 * NUL-padded, and a name is allowed to fill it, so this is not strcmp.
 */
static int
name_is(const struct boot_archive_entry *e, const char *want)
{
	unsigned i;

	for (i = 0; i < BOOT_ARCHIVE_NAMELEN; i++) {
		if (e->name[i] != want[i])
			return 0;
		if (want[i] == '\0')
			return 1;
	}

	/*
	 * Thirty-two bytes matched and none of them was a terminator, so the
	 * field is full; want matches only if it ends exactly there.
	 */
	return want[BOOT_ARCHIVE_NAMELEN] == '\0';
}

/*
 * Turn the loader's one range into the module list.
 *
 * The images are not copied and not moved. They stay where the loader put
 * them, their range is already cut out of free memory, and the linear map
 * will make all of it addressable a moment from now; what the list holds is
 * physical addresses, which is what the generic kernel and VM both expect.
 * arch_boot_proc() reads VM's image straight out of the archive, and hands
 * the memory back when it is done with it.
 *
 * Images are matched by name rather than taken in order. Order is what the
 * 32-bit ports use, because a boot monitor loads their modules in the order
 * its own configuration lists them, and the two lists are maintained
 * together. Here the archive is built by a separate program from arguments a
 * build script writes, and one transposed argument would load pm's image as
 * sched and leave nothing to say so. The names are already in the boot image
 * table; using them costs a string compare per module and turns that mistake
 * into a message.
 */
static void
load_boot_archive(kinfo_t *cbi, phys_bytes start, phys_bytes end)
{
	const struct boot_archive_header *hdr;
	const struct boot_archive_entry *ent;
	phys_bytes total, avail;
	unsigned count, i, m;

	if (end <= start)
		panic("no boot archive: the loader passed no initrd, and "
		    "there is nowhere else the boot images can come from");

	avail = end - start;

	/*
	 * Every field in the archive is naturally aligned within it, so the
	 * archive has to be aligned too. This is worth checking rather than
	 * assuming: the MMU is still off, which makes all of memory
	 * Device-nGnRnE, and an unaligned access there faults whatever
	 * SCTLR_EL1.A says. The fault would arrive as a silent stop, because
	 * the message being printed would be the one describing it.
	 */
	if (start % 8 != 0)
		panic("boot archive at %016lx is not 8-byte aligned", start);

	if (avail < sizeof(*hdr))
		panic("boot archive at %016lx is %lu bytes, shorter than its "
		    "own header", start, (unsigned long)avail);

	hdr = (const struct boot_archive_header *)start;

	if (hdr->magic != BOOT_ARCHIVE_MAGIC)
		panic("no boot archive at %016lx: first word %08x, expected "
		    "%08x", start, hdr->magic, BOOT_ARCHIVE_MAGIC);
	if (hdr->version != BOOT_ARCHIVE_VERSION)
		panic("boot archive is version %u, this kernel reads %u",
		    hdr->version, BOOT_ARCHIVE_VERSION);

	count = hdr->count;
	total = (phys_bytes)hdr->total_size;

	if (total > avail)
		panic("boot archive says %lu bytes, the loader left %lu",
		    (unsigned long)total, (unsigned long)avail);
	if (count == 0 || count > MULTIBOOT_MAX_MODS)
		panic("boot archive holds %u images", count);
	if (sizeof(*hdr) + (phys_bytes)count * sizeof(*ent) > total)
		panic("boot archive index of %u entries does not fit in %lu "
		    "bytes", count, (unsigned long)total);

	ent = hdr->entry;

	for (m = 0; m < NR_BOOT_MODULES; m++) {
		const char *want = image[NR_TASKS + m].proc_name;

		for (i = 0; i < count; i++)
			if (name_is(&ent[i], want))
				break;
		if (i == count)
			panic("boot archive has no image named %s", want);

		if (ent[i].offset > total ||
		    ent[i].size > total - ent[i].offset)
			panic("boot image %s runs past the end of the archive",
			    want);
		if (ent[i].size == 0)
			panic("boot image %s is empty", want);

		cbi->module_list[m].mod_start =
		    start + (phys_bytes)ent[i].offset;
		cbi->module_list[m].mod_end =
		    start + (phys_bytes)(ent[i].offset + ent[i].size);
	}

	cbi->mbi.mi_mods_count = NR_BOOT_MODULES;

	printf("boot archive %016lx-%016lx, %u images\n", start, start + total,
	    count);
}

/*===========================================================================*
 *				get_parameters				     *
 *===========================================================================*/
static void
get_parameters(kinfo_t *cbi, phys_bytes dtb)
{
	struct dtb_scan scan;
	phys_bytes kern_start = (phys_bytes)__kernel_start;
	phys_bytes kern_end = (phys_bytes)__kernel_end;
	int k;

	memset(cbi, 0, sizeof(*cbi));

	/*
	 * The upper-half address, not the one this code sees.
	 *
	 * Everything in cbi is read by kmain(), which runs after the move; a
	 * pointer taken here is physical, because early boot reaches kernel
	 * symbols PC-relatively and the program counter is still low. This is
	 * the only pointer pre_init() puts in the boot information, and
	 * leaving it physical is what made kmain() fault in its first memcpy -
	 * it copies the early message buffer out of exactly this pointer.
	 *
	 * Same reasoning, and the same expression, as vir_kern_start below.
	 */
	cbi->kmessages = (struct kmessages *)((vir_bytes)&kmessages +
	    KERNEL_VA_OFFSET);
	cbi->do_serial_debug = 1;
	cbi->serial_debug_baud = 115200;

	/*
	 * The user address space ceiling is inherited from the 32-bit ports.
	 * It is not protecting anything here - the kernel is in TTBR1, which
	 * EL0 cannot reach at all - but choosing a new one is a decision
	 * about the process address space, and that belongs with VM rather
	 * than here. See port/PORTING-LOG.md, stage 4 group 1.
	 */
	cbi->user_sp = (vir_bytes)USR_STACKTOP;
	cbi->user_end = (vir_bytes)USR_DATATOP;

	/* __kernel_start is physical until the switch; the kernel runs high. */
	cbi->vir_kern_start = (vir_bytes)__kernel_start + KERNEL_VA_OFFSET;

	/*
	 * Nothing to hand back when the bootstrap phase ends. On the 32-bit
	 * ports this is the unpaged copy of the early code, which is dead
	 * once the kernel is relocated; there is no second copy here.
	 */
	cbi->bootstrap_start = 0;
	cbi->bootstrap_len = 0;
	cbi->kernel_allocated_bytes = kern_end - kern_start;

	/*
	 * One board is built in, the way ARM builds in bsp/ti. When there is a
	 * second, the board comes out of the device tree's root compatible
	 * property - which is the reason the tree is parsed here at all.
	 */
	set_param(cbi, BOARDVARNAME, (char *)get_board_name(BOARD_ID_QEMU_VIRT));
	set_param(cbi, ARCHVARNAME,
	    (char *)get_board_arch_name(BOARD_ID_QEMU_VIRT));

	if (!fdt_valid((const void *)dtb))
		panic("no device tree at %lx: nothing says where memory is",
		    dtb);

	memset(&scan, 0, sizeof(scan));
	scan.cbi = cbi;
	if (fdt_walk((const void *)dtb, scan_node, &scan) != 0)
		panic("malformed device tree at %lx", dtb);

	if (cbi->mmap_size == 0)
		panic("device tree describes no memory");

	/*
	 * Everything that is in RAM and is not free: the kernel image, the
	 * device tree itself, and whatever the loader left as an initrd. The
	 * blob is cut out rather than copied because the linear map will keep
	 * it reachable for as long as it is wanted.
	 */
	cut_memmap(cbi, kern_start, kern_end);
	cut_memmap(cbi, dtb, dtb + fdt_size((const void *)dtb));
	if (initrd_end > initrd_start)
		cut_memmap(cbi, initrd_start, initrd_end);

	printf(OS_NAME "/aarch64: %u core%s, memory ", scan.ncpu,
	    scan.ncpu == 1 ? "" : "s");
	print_memmap(cbi);

	load_boot_archive(cbi, initrd_start, initrd_end);

	/*
	 * The multiboot structure is a husk on this architecture: there is no
	 * multiboot loader, and the fields that would point back into its
	 * tables have nothing to point at. What means anything is the module
	 * list and its count, and those load_boot_archive() has just filled in
	 * from the archive the loader was given.
	 *
	 * The kernel is entered in the list as an extra module, as it is on
	 * the other two ports: VM reads it from there to find out what to
	 * leave alone.
	 */
	k = cbi->mbi.mi_mods_count;
	assert(k < MULTIBOOT_MAX_MODS);
	cbi->module_list[k].mod_start = kern_start;
	cbi->module_list[k].mod_end = kern_end;
	cbi->mods_with_kernel = k + 1;
	cbi->kern_mod = k;
}

/*===========================================================================*
 *				pre_init				     *
 *===========================================================================*/
void
pre_init(phys_bytes dtb)
{
	/*
	 * The console first: everything below can fail, and a failure without
	 * a console is a machine that stops without a word. head.S has
	 * already zeroed the BSS, so bsp_ser_init() may keep state.
	 */
	bsp_ser_init();

	/*
	 * kputc() decides whether to echo to the serial line by looking at
	 * the global kinfo, which kmain() does not fill until much later.
	 * Setting the flag here is what makes printf() work for the whole of
	 * early boot; get_parameters() sets the same flag in the copy it is
	 * building, and the boot arguments may still turn it off.
	 */
	kinfo.do_serial_debug = 1;

	/*
	 * The kernel may take memory for itself from here until kmain() says
	 * otherwise, and it has to be allowed to before the device tree scan
	 * below, which calls add_memmap().
	 *
	 * Set rather than defined. Both 32-bit ports write "int
	 * kernel_may_alloc = 1;" at file scope in their own pre_init.c, and
	 * that is a second definition of a variable kernel/table.c already
	 * defines through EXTERN - which links only because those ports are
	 * built with -fcommon, where duplicate definitions are merged. With
	 * -fno-common, the default since GCC 10, it is a link error. This
	 * kernel is built that way on purpose, so the value is assigned here
	 * instead.
	 */
	kernel_may_alloc = 1;

	boot_dtb = dtb;

	get_parameters(&boot_kinfo, dtb);

	/*
	 * The exception vectors, against their physical address. From here on
	 * a fault is diagnosable; before this point one stops the machine
	 * without a word, which is why the console came first. VBAR_EL1 is
	 * written again after the move to the upper half, where the same
	 * expression yields the virtual address.
	 */
	trap_init();

	/*
	 * Where the interrupt controller's registers are, so that the maps
	 * built below cover them. intr_init() itself runs much later, from
	 * kmain(), by which time this address has been rewritten to its
	 * kernel-virtual value by the callback loop in pre_init_high().
	 */
	bsp_intr_pre_init();

	/*
	 * Two maps. The identity map keeps this code addressable for the few
	 * instructions between setting SCTLR_EL1.M and branching high; the
	 * kernel map is where the kernel lives from then on.
	 *
	 * There are no exception vectors yet, so a fault anywhere in here
	 * stops the machine silently. VBAR_EL1 gets written twice - once with
	 * the table's physical address before the MMU comes on, once with its
	 * virtual address after the move - when the exception path lands.
	 */
	pg_identity(&boot_kinfo);
	pg_mapkernel(&boot_kinfo);

	vm_enable_paging();

	pg_enter_high(pre_init_high);
}

/*===========================================================================*
 *				pre_init_high				     *
 *===========================================================================*/
static void
pre_init_high(void)
{
	kern_phys_map *m;

	/*
	 * Move every driver onto its kernel mapping while both addresses
	 * still work. Nothing may print between the console driver's callback
	 * and the end of this loop: its old base is gone and its new one has
	 * not arrived.
	 *
	 * This is the same list, and the same callbacks, that VM drives later
	 * through arch_enable_paging() - the kernel is just the first to
	 * answer, because it maps these ranges for itself in TTBR1 and cannot
	 * wait for a server to start.
	 */
	for (m = kern_phys_map_list(); m != NULL; m = m->next)
		m->cb(m->id, (phys_bytes)phys2vir(m->addr));

	/*
	 * VBAR_EL1 again, now that taking the address of the vector table
	 * yields its upper-half address. This has to happen before the
	 * identity map goes: between the two calls the old value still
	 * points at something mapped, so a fault in here is still reportable.
	 */
	trap_init();

	pg_drop_identity();

	kmain(&boot_kinfo);
}
