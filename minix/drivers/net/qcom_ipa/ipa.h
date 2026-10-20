/*
 * qcom_ipa: the IPA v2.6L of a Qualcomm SoC (the Mi Pad 4's SDM660) - the
 * modem's data path to the AP (port/clover/hw-modem.md, section 9).
 *
 * The register numbers, the order of operations and the protocol are the
 * ipa2-lite driver's (github.com/valam84/linux, nura/sdm660-ipa,
 * drivers/net/ipa2-lite; GPL - taken as facts, not as text).
 */
#ifndef _QCOM_IPA_H
#define _QCOM_IPA_H

#include <minix/drivers.h>
#include <minix/log.h>
#include <minix/netdriver.h>

#include <stdint.h>

/* One window: the BAM, the IPA's registers and its SRAM. */
#define IPA_BAM			0x4000
#define IPA_REGS		0x40000
#define IPA_SRAM		0x45000

/* The BAM. */
#define BAM_CTRL		(IPA_BAM + 0x0000)
#define BAM_REVISION		(IPA_BAM + 0x1000)
#define BAM_NUM_PIPES		(IPA_BAM + 0x1008)
#define BAM_P_CTRL(n)		(IPA_BAM + 0x13000 + 0x1000 * (n))

/* The IPA. */
#define IPA_COMP_HW_VERSION	(IPA_REGS + 0x0030)
#define IPA_VERSION		(IPA_REGS + 0x0034)
#define IPA_COMP_CFG		(IPA_REGS + 0x0038)
#define IPA_ROUTE		(IPA_REGS + 0x0044)
#define IPA_SHARED_MEM		(IPA_REGS + 0x0050)
#define IPA_ENABLED_PIPES	(IPA_REGS + 0x05dc)
#define IPA_IRQ_STTS_EE0	(IPA_REGS + 0x1008)
#define IPA_IRQ_EN_EE0		(IPA_REGS + 0x100c)
#define IPA_IRQ_CLR_EE0		(IPA_REGS + 0x1010)

/* The microcontroller's mailbox, at the start of the SRAM. */
#define IPA_UC_RESP		(IPA_SRAM + 0x08)
#define IPA_UC_RESP_PARAM	(IPA_SRAM + 0x0c)

/* The endpoints (BAM pipes) the AP's side has, ipa2-lite's numbering. */
#define EP_TEST_TX		0
#define EP_TEST_RX		1
#define EP_LAN_RX		2
#define EP_CMD			3
#define EP_TX			4	/* to the modem */
#define EP_RX			5	/* from the modem */
#define EP_NUM			6
#define IPA_NUM_PIPES		20	/* the filter table's width */

/* A pipe's ring: 256 descriptors of 8 bytes. */
#define FIFO_NDESC		256
#define DESC_BYTES		8
#define FIFO_BYTES		(FIFO_NDESC * DESC_BYTES)

/* The SRAM's parts (ipa.h of ipa2-lite). */
#define MEM_DRV			0
#define MEM_FT_V4		1
#define MEM_FT_V6		2
#define MEM_RT_V4		3
#define MEM_RT_V6		4
#define MEM_MDM_HDR		5
#define MEM_MDM_COMP		6
#define MEM_MDM			7
#define MEM_END			8

/* The ARM SMMU v2 (Linux's arm-smmu.h). */
#define SMMU_SCR0		0x000
#define SMMU_IDR0		0x020
#define SMMU_IDR1		0x024
#define SMMU_IDR2		0x028
#define SMMU_IDR7		0x03c
#define SMMU_SGFSR		0x048
#define SMMU_SMR(n)		(0x800 + 4 * (n))
#define SMMU_S2CR(n)		(0xc00 + 4 * (n))
#define SMMU_CBAR(n)		(4 * (n))	/* in the second page */
#define SMMU_CB_SCTLR		0x000
#define SMMU_CB_TCR		0x030
#define SMMU_CB_TTBR0		0x020
#define SMMU_CB_FSR		0x058

#define IPA_MAXSID		4

/* A buffer the IPA reaches: this service's pages, at an IOVA. */
struct dbuf {
	void *va;
	phys_bytes pa;
	uint32_t iova;
	size_t len;
};

struct bam_desc {
	uint32_t addr;
	uint16_t size;			/* or an immediate command's opcode */
	uint16_t flags;
} __attribute__((packed));

struct pipe {
	struct dbuf fifo;
	volatile struct bam_desc *desc;
	unsigned head, tail;
};

struct ipa_part {
	uint32_t offset, size;		/* from the start of the SRAM */
};

struct ipa_hw {
	struct pipe pipe[EP_NUM];
	struct ipa_part part[MEM_END + 1];
	uint32_t smem_restr, smem_size;
	uint32_t drv_at;		/* DRV's first word, in the window */
	struct dbuf sys_hdr;
	int uc_loaded;
};

extern struct ipa_hw hw;

struct ipa {
	u64_t pa, sz;
	volatile uint8_t *base;

	u64_t smmu_pa, smmu_sz;
	volatile uint8_t *smmu;
	uint32_t sid[IPA_MAXSID];
	unsigned nsid;
};

extern struct ipa ipa;
extern struct log ilog;

/* ipa_probe.c */
int ipa_find(const void *dtb);
volatile uint8_t *ipa_map(u64_t pa, u64_t sz, const char *what);
volatile uint8_t *ipa_map(u64_t pa, u64_t sz, const char *what);
int ipa_probe_smmu(int banks);
int ipa_probe_regs(void);

/*
 * A line, then a pause, before each step of the bring-up: on the tablet a
 * step reset the SoC into Qualcomm's crash-dump mode, and only a log
 * streamed off the machine as it is written (kmsg -f over ssh) survives
 * that (hw-modem.md, 9.2).  step_ms= sets the pause, 0 for none.
 */
extern unsigned step_ms;
#define STEP(...)	do { log_info(&ilog, __VA_ARGS__); \
			    if (step_ms) micro_delay(step_ms * 1000); } while (0)

/* ipa_hw.c */
int dbuf_alloc(struct dbuf *b, size_t len, int writable);
void dbuf_clean(struct dbuf *b, size_t off, size_t len);
void dbuf_inval(struct dbuf *b, size_t off, size_t len);
int pipe_put(unsigned ep, const struct bam_desc *d, unsigned n);
int ipa_hw_init(void);
int ipa_uc_poll(void);
extern int ipa_test_mode;
int ipa_loop_test(void);

/* ipa_net.c */
struct netdriver_data;
int ipa_net_init(unsigned mux);
void ipa_net_modem(int ready);
unsigned int ipa_net_link(uint32_t *media);
void ipa_net_hwaddr(netdriver_addr_t *a);
int ipa_net_send(struct netdriver_data *data, size_t size);
ssize_t ipa_net_recv(struct netdriver_data *data, size_t max);
void ipa_net_tick(void);

/* ipa_smmu.c */
extern int smmu_check;
int ipa_smmu_init(void);
int ipa_smmu_map(uint32_t iova, phys_bytes pa, size_t len, int writable);
int ipa_smmu_route(void);
void ipa_smmu_report(void);

/* ipa.c */
int ipa_rpm_clock(uint32_t khz);

/*
 * Every register access one instruction, the value in a real register:
 * the hypervisor emulates some of the SMMU's registers by the faulting
 * instruction's syndrome (port/clover/hw-gpu.md, section 16).
 */
static inline uint32_t
rd32(volatile uint8_t *b, uint32_t off)
{
	uint32_t v;

	__asm__ volatile("ldr %w0, [%1]" : "=r" (v) : "r" (b + off) :
	    "memory");
	return v;
}

static inline uint64_t
rd64(volatile uint8_t *b, uint32_t off)
{
	uint64_t v;

	__asm__ volatile("ldr %0, [%1]" : "=r" (v) : "r" (b + off) :
	    "memory");
	return v;
}

static inline void
wr32(volatile uint8_t *b, uint32_t off, uint32_t v)
{
	__asm__ volatile("str %w0, [%1]" : : "r" (v), "r" (b + off) :
	    "memory");
}

#endif /* _QCOM_IPA_H */
