#ifndef _BSP_INTR_H_
#define _BSP_INTR_H_

/*
 * The interrupt controller, as the platform provides it. The same set ARM
 * has, so that the contract keeps its shape across the two architectures -
 * which is where the bsp_ prefix comes from, and on this architecture it is
 * now historical. The implementation is arch/aarch64/gic.c and the two
 * version-specific files beside it, not a per-board driver: a board does not
 * settle which GIC it has in a way a build can read, so the device tree is
 * asked. QEMU's virt machine alone answers either version.
 *
 * bsp_irq_handle() is called from the IRQ vector with nothing decided yet: it
 * is the controller that knows which line fired and how to retire it - two
 * accesses to the CPU interface, which is memory on GICv2 and a pair of
 * system registers on GICv3. Everything above it - the hook chain, the
 * notification to the driver - is generic, and this reaches it by calling
 * irq_handle().
 *
 * bsp_intr_pre_init() is the odd one out and is not in ARM's contract. It
 * registers the controller's registers with kern_phys_map before paging
 * exists, which has to happen inside pre_init() while the mappings are still
 * being built; intr_init() itself runs much later, from kmain(), by which
 * time the addresses have been rewritten to their kernel-virtual values.
 * ARM does not need it because bsp_init() runs early enough there.
 */
void bsp_intr_pre_init(void);

int intr_init(int auto_eoi);

void bsp_irq_handle(void);
void bsp_irq_unmask(int irq);
void bsp_irq_mask(int irq);

/* How many interrupt IDs this controller actually implements. */
int bsp_irq_lines(void);

#endif /* _BSP_INTR_H_ */
