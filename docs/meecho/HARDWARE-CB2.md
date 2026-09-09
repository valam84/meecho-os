# The target board: BIGTREETECH CB2

A Rockchip RK3566 module: four Cortex-A55 cores (ARMv8.2), LPDDR4, on-board
eMMC, gigabit Ethernet, and a serial console. It replaced a Raspberry Pi CM4 as
the target in September 2026, and everything below was verified against the
board's own device tree — the board is in mainline Linux from 6.14 as
`arch/arm64/boot/dts/rockchip/rk3566-bigtreetech-cb2.dts`.

## What the device tree says, and what it cost

| | Answer from the tree | What it meant |
|---|---|---|
| SoC and core | `rockchip,rk3566`, `arm,cortex-a55` ×4 | ARMv8.2, so PAN exists (8.1) and LSE atomics do too. `-mcpu` is not set at all today, so choosing one is tuning, not work |
| Console UART | `snps,dw-apb-uart` (+`rockchip,rk3568-uart`), `uart2` at `0xfe660000`, `reg-shift=2`, `reg-io-width=4`, console `serial2:1500000n8` | **Not a PL011.** Both drivers exist: `dw8250.c` under the kernel's `serial.c`, `ns8250.c` beside `pl011.c` in `tty` behind the table in `uart.h`. The port is found through `/chosen/stdout-path` |
| GIC version | `arm,gic-v3`: GICD `0xfd400000`, GICR `0xfd460000` (512 KB), plus an ITS. **No CPU interface region at all** | GICv3 was written for this. There was never a legacy mode to fall back to — GICC is not declared |
| RAM | measured on the board: `0x00200000`–`0x7fffffff`, 2 GB LPDDR4; the first 2 MB belong to BL31, CMA and OP-TEE reservations start at `0x6b600000` | `_kern_phys_base = 0x40200000` lands in the free middle. The image must stay where it is put: the header carries **flags bit 3**, without which U-Boot is required to relocate it |
| PSCI | `arm,psci-1.0`, `method = "smc"` | Works as is; `psci.c` reads the method from the tree and knows both instructions |
| eMMC | `mmc@fe310000`, `rockchip,rk3568-dwcmshc` — **SDHCI, not dw-mshc** — 8-bit, up to 200 MHz, non-removable, IRQ 51 | Driver written (`sdmmc`) |
| SD card | `mmc@fe2b0000`, `rockchip,rk3568-dw-mshc`, 4-bit, IRQ 130 | Different registers, different file. **No driver** |

**`rockchip,rk3568-dwcmshc` is not `dw-mshc`** despite the names: it has the
standard SD Host Controller registers and an ordinary divider. The card slot is
the DesignWare part, with its own clock-update command.

## Four things not in the SDHCI specification

Without every one of them the controller does not work, and each fails in its
own way. This is the clearest example in the project of why a bench cannot
replace a board.

- **`MISC_INTCLK_EN`**, bit 1 of `MISC_CON` at offset `0x81c`. Out of reset it
  is clear, the register is outside the standard block, and a software reset
  does not restore it — so the internal clock never starts at all.
- **The clock source decides how responses are read.** With the delay line
  bypassed, the controller samples the card's response against the *source*
  clock, not the divided one. Keeping the source at 198 MHz and dividing means
  clocking the card correctly and reading its answer at the wrong moments: a
  48-bit response arrives with bits missing and looks plausible, a 136-bit one
  does not arrive at all. What has to move is the multiplexer in the CRU — hence
  the `rockchip,rk3568-cru` permission in `system.conf`, and hence "exactly
  375 kHz" for identification: it is the only input below a megahertz.
- **Auto CMD12 does not work.** Use CMD23.
- **Transfer length is capped by the controller's buffer**: four blocks pass,
  eight do not. The flow control the standard promises is absent in this part.
  That is a property of the programmed path only. Through ADMA2 the engine
  takes the bytes itself and the ceiling does not apply, so the driver keeps
  both numbers and picks by whether the buffer has a physical address.

## Ethernet

| | |
|---|---|
| Controller | `rockchip,rk3568-gmac` + `snps,dwmac-4.20a` at `0xfe010000`, 64 KB. Synopsys ID `0x51` — that is **DWMAC 5.10**, not 4.x as the compatible string reads |
| Interrupts | three: `macirq` SPI 32 (GIC line 64), `eth_wake_irq` SPI 29, `eth_lpi` SPI 28. Only the first is used |
| PHY | **Motorcomm YT8531**, address 0 on the MDIO bus inside the node, ID `0x4f51e91b` |
| Mode | RGMII, `tx_delay = 0x30`, `rx_delay = 0x10` — delay lines programmed in the GRF by the Rockchip glue |
| Clock | `clock_in_out = "input"` — the PHY supplies 125/25 MHz, not the SoC |
| Speed | **not a bit but a frequency**: the glue changes `clk_mac1_speed` in the CRU — 2.5 / 25 / 125 MHz for 10 / 100 / 1000 |
| Link | 100 Mbit/s on this site, so the gigabit path cannot be tested here |

The computed recipe for this board is in
[`port/cb2-gmac/registers.md`](../../port/cb2-gmac/registers.md); a register
dump from the running vendor system is in `linux-reference.txt` and `phy-reference.txt`
beside it. Fifteen pins on function 3 in GPIO3's IOMUX, plus **the route bit
`GRF + 0x0300` bit 8 = 0** that selects pin set m0 over m1 — without which
nothing else means anything, and which is visible only in somebody else's code.
The bootloader cannot be relied on for any of it: this U-Boot has no
`CONFIG_NET` at all.

> **The reference is the vendor kernel that is on the board, not the newest
> code for the same chip.** Mainline turns the PHY's delays off for `rgmii`;
> the vendor's `yt8531_config_init` leaves them alone and writes the analogue
> part — CLK_OUT at 125 MHz, RXC duty cycle, drive strength, and the 100M
> transmit reference voltage. A driver written from mainline brings the link up
> and carries nothing. The board settled it itself: its own `dmesg` names the
> function it executed.

## Other blocks in use

- **OTP** (`rockchip,rk3568-otp` at `0xfe38c000`): the station address is
  derived from the `soc-id` cell at offset `0x0a` by the same recipe the vendor
  kernel uses — two `jhash` rounds with keys `0x35660001/2`. The board yields
  `8a:6e:41:09:8e:2f`, **the same address it has under vendor Linux**, so the
  same DHCP lease and the same ARP entry. Checked on a host bench
  (`port/test/dwmac/socidtest.c`) against numbers taken from the board *before*
  the first run.
- **TRNG** (`rng@fe388000`): the entropy source. The vendor keeps the node
  `disabled`, but `clk_summary` showed its clocks running and the block answered
  when polled from vendor Linux through `/dev/mem` with no clock or reset setup
  of its own (`port/cb2-trng/probe.py`). Nothing is claimed about the quality of
  its numbers.
- **GRF** at `0xfdc60000`, **CRU** at `0xfdd20000`, **GPIO0** at `0xfdd60000`
  (the PHY reset is pin 21, active low).

## What the board taught that QEMU could not

Seven fixes stood between the first boot and a prompt, and **none of them
reproduces on the emulator**:

- **The instruction cache is not coherent with the data cache.** `exec` writes a
  program with ordinary stores through the linear map, instruction fetch does
  not look there, and the instruction cache is physically indexed — so a process
  executes the text of the page's previous owner. Every boot broke differently,
  and the "pointer" in the fault turned out to be a pair of AArch64
  instructions. `arch_proc_init()` now cleans D-cache to PoU where `CTR_EL0.IDC`
  requires it, and invalidates I-cache.
- **The ASID scheme** — TCG's software TLB is not tagged and is flushed on every
  switch, so the emulator can show the scheme is correct and can never show why
  it exists.
- **Page memory attributes** are ignored by TCG entirely.
- **A silent interrupt storm looks exactly like a hang.** Three boots printed
  the banner and then only echoed. The process table showed `cur=tty` with a
  shrinking quantum and 7.6 million UART interrupts: on DesignWare, a character
  timeout with an empty FIFO is cleared only by reading RBR, and "no interrupt"
  with the line asserted only by reading `USR`. Neither is visible on QEMU's
  PL011.
