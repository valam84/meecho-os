# RK3566/RK3568 VOP2: VP0 + Smart0 → HDMI0, XRGB8888, 1280x720p60

Register-level recipe for one configuration, cross-checked against the vendor
Linux snapshot `port/cb2-display/ref-720p-forced.txt` (CB2, kernel
6.1.115-btt-rockchip64, same mode).

Sources (facts only, no code copied):

- `vendor-rockchip_drm_vop2.c` (V): `vop2_initial`, `vop2_layer_map_initial`,
  `vop2_crtc_atomic_enable`, `vop2_calc_cru_cfg`, `vop2_get_hdmi_pol`,
  `vop2_post_config`, `vop2_dither_setup`, `vop2_cfg_update`,
  `vop2_crtc_setup_output_mode`, `vop2_crtc_atomic_begin`,
  `vop2_calc_bg_ovl_and_port_mux`, `vop2_setup_port_mux`,
  `vop2_setup_layer_mixer_for_vp`, `vop2_setup_alpha`, `vop2_parse_alpha`,
  `vop2_setup_dly_for_vp`, `vop2_setup_dly_for_window`, `vop2_setup_hdr10`,
  `vop2_win_atomic_update`, `vop2_setup_scale`, `vop2_scale_factor`,
  `rk3568_vop2_cfg_done`, `vop2_read_vcnt`, `vop2_mask_write`,
  `vop2_crtc_enable_vblank`, `vop2_debug_irq_enable`, `vop2_axi_irqs_enable`,
  `vop2_crtc_atomic_disable`, `vop2_crtc_set_color_bar`, `vop2_crtc_reset`.
- `vendor-rockchip_vop2_reg.c` (R): `rk3568_vop_ctrl`, `rk3568_vop_vp0_regs`,
  `rk3568_vop_video_ports`, `rk3568_vp0_intr` / `rk3568_vop_intrs`,
  `rk3568_vop_axi_intr`, `rk3568_vop_layer0..7_regs`, `rk3568_vop_win_data`,
  `rk3568_esmart_win_data`, `rk3568_esmart_win_scl`, `rk3568_vop_wb_regs`,
  `rk3568_sys_grf_ctrl`, `rk3568_vop`, `rk3568_vp_plane_mask`.
- `vendor-rockchip_drm_vop.h` (H): interrupt bit names, alpha/scale enums,
  `ROCKCHIP_OUT_MODE_*`, `HSYNC_POSITIVE`... .
- Offsets: `rockchip_vop_reg.h` is NOT in `ref/`; the `RK3568_*` numbers were
  read from rockchip-linux/kernel `develop-6.1`
  `drivers/gpu/drm/rockchip/rockchip_vop_reg.h` and cross-checked with mainline
  v6.12 `rockchip_drm_vop2.h` (field names like `__STANDBY`, `__HDMI_PIN_POL`).
  Clock facts: mainline v6.12 `drivers/clk/rockchip/clk-rk3568.c`. IOMMU
  facts: mainline v6.12 `drivers/iommu/rockchip-iommu.c`. Those three files
  should be copied into `ref/` if the offsets are to be re-audited offline.

---

## 1. Address map

| What | Address | Notes |
|---|---|---|
| VOP2 registers | `0xfe040000`, size `0x3000` | DT `reg[0]` "regs" (live.dtb) |
| Gamma LUT | `0xfe044000`, size `0x1000` | DT `reg[1]` "gamma_lut"; unused here (`DSP_LUT_EN`=0) |
| VOP IOMMU | `0xfe043e00` + `0xfe043f00`, `0x100` each | `rockchip,iommu-v2`, two banks; clocks = aclk_vop + hclk_vop |
| IRQ | GIC SPI 148 = INTID 180, level-high | one line for SYS + all VPs, **shared with the IOMMU** |

Blocks inside the 0x3000 window (RK3568 offsets):

| Block | Offset | Size |
|---|---|---|
| SYS (cfg done, IF, IRQs, WB, status) | `0x0000` | `0x100` used |
| OVL (overlay: layer sel, port sel, mixers, delays) | `0x0600` | `0x100` |
| VP0 / VP1 / VP2 | `0x0C00` / `0x0D00` / `0x0E00` | `0x100` each |
| Cluster0 / Cluster1 | `0x1000` / `0x1200` | `0x200` each |
| Esmart0 / Esmart1 | `0x1800` / `0x1A00` | `0x200` each |
| **Smart0** / Smart1 | **`0x1C00`** / `0x1E00` | `0x200` each |
| HDR (SDR2HDR/HDR2SDR, tables) | `0x2000` | |

Window bases in `rk3568_vop_win_data` are relative to Esmart0 (`0x1800`):
Smart0 `.base = 0x400` → `0x1C00`. All Esmart/Smart windows share the register
layout `rk3568_esmart_win_data` + `rk3568_esmart_win_scl`.

RK3566 specifics (CB2): `OTP_WIN_EN` must be 1 (`vop2_initial`), and
Cluster1/Esmart1/Smart1 are "mirror" windows without their own fetch
(`WIN_FEATURE_MIRROR`, `vop2_is_mirror_win`) — which is why the Linux plane list
shows only Smart0/Esmart0/Cluster0.

---

## 2. Prerequisites outside the VOP (state on the snapshot)

These are not VOP registers, but nothing below works without them.

- **Power domain PD_VO** (DT `power-domains = <pmu 9>`) on. Snapshot genpd: `vo on`.
- **Clocks before any register access.** `vop2_initial` calls
  `vop2_core_clks_prepare_enable` (hclk, aclk, pclk) *before* the first
  register read (the `regsbak` memcpy). The dclk is enabled at the end of
  `vop2_initial`; its rate is set in `vop2_crtc_atomic_enable` *before*
  `cfg_done` and standby release. Expect a bus hang/SError if registers are
  touched with hclk gated (general Rockchip behaviour; not tested here).
- Snapshot clock state, decoded with `clk-rk3568.c`:

| Clock | Rate | Register evidence (snapshot) |
|---|---|---|
| HPLL (PMUCRU `PLL_CON(16)` = `0xfdd00040`) | 74.25 MHz | CON0 `0x4063`: postdiv1=4, fbdiv=99; CON1 `0x1502`: dsmpd=1, lock=1, postdiv2=4 (bits 8:6), refdiv=2 → 24/2·99/4/4 = 74.25. PMU_MODE_CON0 (`+0x80`)=`0x5`: hpll [3:2]=1 (normal) |
| dclk_vop0 | 74.25 MHz | CRU `CLKSEL_CON(39)` (`0xfdd2019c`)=0: mux [11:10]=0 (hpll), div [7:0]=0 (/1). DT `assigned-clock-parents` = PMUCRU clock 2 (HPLL) |
| aclk_vop_pre / aclk_vop | 396 MHz | `CLKSEL_CON(38)` (`+0x198`)=`0x42`: mux [7:6]=1 (gpll 1188), div [4:0]=2 (/3) |
| aclk_vo / hclk_vo(=hclk_vop) / pclk_vo | 297 / 148.5 / 74.25 MHz | `CLKSEL_CON(37)` (`+0x194`)=`0x3100` |
| Gates | | `CLKGATE_CON(20)` (`+0x350`)=`0x3800`: only bits 11,12,13 gated (dclk_vop1, dclk_vop2, clk_vop_pwm); bits 0,1,2,6,8,9,10 (aclk_vo, hclk_vo, pclk_vo, aclk_vop_pre, aclk_vop, hclk_vop, dclk_vop0) open |

  CRU/PMUCRU registers are hiword-masked. HPLL also feeds `clk_hdmi_ref`, so the
  HDMI side depends on the same PLL.
- **No VOP reset in the DT** (no `resets` property; the vendor asks for an
  optional `dclk_vp0` reset that does not exist). The driver never resets the
  VOP; it relies on the SoC reset state. After a PSCI `SYSTEM_RESET` the VOP is
  in reset defaults; do not rely on any other prior state — write every
  register in §8.
- **IOMMU.** Linux attaches the IOMMU, so the snapshot holds an **IOVA**
  (`YRGB_MST` = 0). The vendor attaches it lazily in the first
  `vop2_crtc_atomic_flush` (`!is_iommu_enabled && is_iommu_needed` →
  `rockchip_drm_dma_attach_device`), i.e. the VOP is designed to scan out
  before the IOMMU exists (bootloader logo handover). With paging disabled, the
  IOMMU passes VOP AXI addresses through untranslated, so `YRGB_MST` is a
  **physical address**. Check both banks: `MMU_STATUS` (`+0x04`) bit 0
  `PAGING_ENABLED` must read 0 at `0xfe043e04` and `0xfe043f04`; if it does
  not, `MMU_COMMAND` (`+0x08`) = 1 (`DISABLE_PAGING`). `YRGB_MST` is 32 bits
  on RK3568, so the framebuffer must be below 4 GiB. On CB2 all RAM is (RAM
  `0x00200000`–`0x7fffffff`). Keep it out of the reserved CMA/OP-TEE area from
  `0x6b600000` up, because the VOP is a non-secure master.
- **Cache coherency.** The VOP is a non-coherent DMA master. Either map the
  framebuffer Normal non-cacheable (write-combine), or clean it to PoC
  (`sys_cachectl CACHE_CLEAN`) after drawing.

---

## 3. Register conventions

**Hiword ("write-enable in [31:16]") registers in VOP2 are few.** These are
`VOP_REG_MASK` in R. `vop2_mask_write` writes `(v<<shift) | (mask<<(shift+16))`.

| Register | Offset | Masked bits |
|---|---|---|
| VP0/VP1/VP2 `INT_EN` | `0x0A0` / `0x0B0` / `0x0C0` | [15:0], enable in [31:16] |
| VP0/VP1/VP2 `INT_CLR` | `0x0A4` / `0x0B4` / `0x0C4` | [15:0] |
| `SYS0_INT_EN` / `SYS0_INT_CLR` | `0x080` / `0x084` | [7:1] |
| `SYS1_INT_EN` / `SYS1_INT_CLR` | `0x090` / `0x094` | [7:1] |
| `REG_CFG_DONE` bit 14 (write-back cfg done) | `0x000` | bit 14, enable bit 30 |

**Everything else is a plain 32-bit register.** That includes `REG_CFG_DONE` bits
[2:0] (VP0..VP2 cfg done) and bit 15 (`GLB_CFG_DONE_EN`). On RK3568 these have
**no** write mask (comment in `rk3568_vop2_cfg_done`; RK3588 has masks at
[18:16]). So every `REG_CFG_DONE` write must carry bit 15. It must also OR in
any other VP's still-pending bits; with a single VP this reduces to always
writing `0x8001`. Masked registers read back only the low half.

**Shadow and commit.** Most VP, OVL and window registers are double-buffered.
They take effect at the next frame start of the owning VP after that VP's bit
in `REG_CFG_DONE` is set. The bit reads 1 until the frame start latches it,
then 0 (`vop2_vp_done_bit_status`). Windows follow the VP selected for them in
`OVL_PORT_SEL`. The exceptions take effect **immediately**:

- `DSP_IF_EN`/`DSP_IF_CTRL`/`DSP_IF_POL`, when `DSP_IF_POL` bit 28
  (`if_ctrl_cfg_done_imd`) = 1;
- `OVL_LAYER_SEL`/`OVL_PORT_SEL`, when `OVL_CTRL` bit 28
  (`ovl_port_mux_cfg_done_imd`) = 1;
- clearing `DSP_CTRL.STANDBY` (bit 31). The comment in
  `vop2_crtc_atomic_enable` says the VP starts scanning at once with whatever
  timing is configured. **Setting** standby completes only at the end of the
  current frame, signalled by `DSP_HOLD_VALID` (comment in
  `vop2_crtc_atomic_disable`).

**Readback is not the written value until it is latched.** For
`OVL_LAYER_SEL`/`OVL_PORT_SEL` the vendor polls readback until it equals what
it wrote (`vop2_wait_for_layer_cfg_done`, `vop2_wait_for_port_mux_done`;
`rk3568_vop2_cfg_done` compares `readl(LAYER_SEL)` with the software copy).
The driver does every read-modify-write against a software shadow (`regsbak`),
which it fills **once from the hardware** at init. Do the same: keep a shadow,
and verify by readback only after the done bit has cleared. Because of that
initial memcpy, reset-default bits in partially written registers survive
(see `LINE_FLAG` and `OVL_LAYER_SEL` in §9).

---

## 4. Ordered bring-up from reset

Values are for this mode. Field decodes are in §5–§7, and the flat table is in §8.

**A. Global init** (`vop2_initial`, run once while `enable_count == 0`)

1. `0x050 OTP_WIN_EN = 0x1`. Required on RK3566 (`vop2_soc_is_rk3566()`).
2. *(Optional, write-back only.)* `0x040 WB_CTRL`: axi_yrgb_id=0xd, axi_uv_id=0xe,
   then the WB cfg done (`0x000 = 0x4000C000`). Not needed for scan-out.
3. `0x000 REG_CFG_DONE = 0x00008000`: `cfg_done_en` (bit 15).
4. `0x008 SYS_AUTO_GATING_CTRL = 0x0`: clears bit 31 `auto_gating_en`. The
   vendor workaround is for "display image shift when a window enabled".
5. `0x600 OVL_CTRL = 0x10000000`: bit 28, layer/port select take effect
   immediately.
6. `0x030 DSP_IF_POL |= 1<<28`: interface registers take effect immediately.
7. Layer map (`vop2_layer_map_initial`), written directly as the final values:
   `0x608 OVL_PORT_SEL = 0x00000755`, then `0x604 OVL_LAYER_SEL = 0x54706213`.
   Optionally poll `readl(0x608) & 0xffff == 0x0755`.
8. AXI bus-error IRQs (`vop2_axi_irqs_enable`): `0x084 = 0x00020002`,
   `0x080 = 0x00020002`, `0x094 = 0x00020002`, `0x090 = 0x00020002`.
9. Debug IRQ (`vop2_debug_irq_enable`): VP0 `POST_BUF_EMPTY` (bit 4),
   `0x0A4 = 0x00100010`, `0x0A0 = 0x00100010`. Optional; for polling, the raw
   status is enough.
10. Enable dclk_vp0 (CRU gate) and set it to 74.25 MHz.

**B. Interface** (`vop2_crtc_atomic_enable`, HDMI0 branch). Immediate because of A6.

11. `0x028 DSP_IF_EN = 0x00000002`: `hdmi0_en` (bit 1) = 1, `hdmi0_mux`
    [11:10] = 0 (VP0).
12. `0x030 DSP_IF_POL = 0x100000b0`: bit 28 imd; `hdmi_dclk_pol` (bit 7) = 1
    (always 1 for HDMI); `hdmi_pin_pol` [6:4] = `0b011` (bit 4 HSYNC
    positive, bit 5 VSYNC positive, bit 6 DEN negative = 0).
    `vop2_get_hdmi_pol`: on non-RK3588, a bit **set** = positive polarity.
13. `0x02C DSP_IF_CTRL = 0` (no dual LVDS/BT; reset state).

**C. VP0 timing, post block, colour path.** Keep VP0 in standby while doing this.

14. `0xC00 VP0_DSP_CTRL = 0x8001000f`. This sets standby=1, `pre_dither_down_en`
    (bit 16)=1, `out_mode` [3:0]=0xF; everything else 0 (`core_dclk_div`,
    `p2i_en`, `dsp_filed_pol`, `dsp_interlace`, `dsp_data_swap`, `x_mir`,
    `post_dsp_out_r2y`, `dither_down_en`, `dither_down_sel`, `dsp_lut_en`).
15. `0xC04 VP0_DUAL_CHANNEL_CTRL = 0`: `dclk_div2` (bit 4) and
    `dclk_div2_phase_lock` (bit 5) are 0 because the output is not YUV420
    (`vop2_calc_cru_cfg`).
16. Timing (`vop2_crtc_atomic_enable`):
    - `0xC48 DSP_HTOTAL_HS_END = (1650<<16) | 40 = 0x06720028`
    - `0xC4C DSP_HACT_ST_END = (260<<16) | 1540 = 0x01040604`
      (`hact_st = htotal − hsync_start`)
    - `0xC54 DSP_VACT_ST_END = (25<<16) | 745 = 0x001902e9`
      (`vact_st = vtotal − vsync_start`)
    - `0xC50 DSP_VTOTAL_VS_END = (750<<16) | 5 = 0x02ee0005`
17. `0x070 VP0_LINE_FLAG`: fields [12:0] and [28:16] = 745 (`act_end`). Do a
    read-modify-write so bits 30:29 keep their reset value; the snapshot reads
    `0x62e902e9`.
18. Post/overscan block (`vop2_post_config`; margins default to 100,
    `vop2_crtc_reset`):
    - `0xC34 POST_DSP_HACT_INFO = 0x01040604`
    - `0xC38 POST_DSP_VACT_INFO = 0x001902e9`
    - `0xC3C POST_SCL_FACTOR_YRGB = 0x10001000`, which is ((src−1)<<12)/(dst−1)
      = `0x1000`, i.e. 1:1 in both halves
    - `0xC40 POST_SCL_CTRL = 0` (no post scale-down)
19. `0xC2C VP0_DSP_BG = 0` (black; R/G/B are 10-bit at [29:20]/[19:10]/[9:0]).
    `vop2_cfg_update` gives 0 for RGB overlay.
20. BCSH bypass (`vop2_tv_config_update` → `vop2_bcsh_reg_update`):
    - `0xC60 BCSH_CTRL = 0x88`: r2y/y2r enables 0, csc modes 2; the mode
      fields are don't-care while the enables are 0.
    - `0xC6C BCSH_COLOR_BAR = 0`: `bcsh_en` bit 31 = 0.
21. HDR path bypass (`vop2_setup_hdr10`, SDR in/out): `OVL_CTRL` bits 4/5
    (`hdr10_en`, `sdr2hdr_path_en`) = 0. `0x2010 SDR2HDR_CTRL = 0x300` (bit 8
    bypass=1; bit 9 auto-gating is the reset value). `0x2020 HDR2SDR_CTRL =
    0x300` (bit 8 bypass=1).

**D. Overlay mixers and pipeline delays** (`vop2_crtc_atomic_begin`)

22. Mixers MIX0..MIX4 (`vop2_setup_alpha`, the "transfer alpha" loop for the
    unused mixers of VP0's six-window chain). For n = 0..4, at
    `0x650 + n·0x10`: `SRC_COLOR=0x20`, `DST_COLOR=0x00ff0060`,
    `SRC_ALPHA=0x20`, `DST_ALPHA=0x74`. `0x6C0 HDR0_SRC_COLOR_CTRL = 0`.
23. Delays:
    - `0x6E0 VP0_BG_MIX_CTRL = 0x2a000000`: `bg_dly` [31:24] = 42.
    - `0xC30 VP0_PRE_SCAN_HTIMING = ((42 + 1280/2 − 1)<<16) | 40 = 0x02a90028`
      (`vop2_setup_dly_for_vp`).
    - `0x6F8 SMART_DLY_NUM`: Smart0 [23:16] = 20 (`dly[DEFAULT]`). The snapshot
      shows `0x14141414`; write that.
    - `0x6F0 CLUSTER_DLY_NUM = 0`.
24. `0x600 OVL_CTRL = 0x00000000`. This clears imd bit 28, as at the end of
    `vop2_setup_layer_mixer_for_vp`; sets `ovl_cfg_done_port` [31:30] = 0 (VP0)
    and `overlay_mode` bit 0 = 0 (RGB).

**E. Smart0 window** (`vop2_win_atomic_update`, in vendor order)

25. `0x1C14 REGION0_YRGB_MST = FB_PHYS` (32-bit physical address).
26. `0x1C10 REGION0_CTRL` format fields: format [5:1] = 0 (`VOP2_FMT_ARGB8888`,
    also used for XRGB8888), `rb_swap` b14=0, `uv_swap` b16=0, `rg_swap` b18=0,
    `argb1555` b7=0, `dither_up` b12=0, vsd gt2/gt4 b8–b11 = 0.
27. `0x1C1C REGION0_VIR = 0x00000500`: `yrgb_vir` [15:0] = pitch/4 = 1280
    32-bit words.
28. Scale (`vop2_setup_scale`, src == dst so `SCALE_NONE`):
    - `0x1C34 SCL_FACTOR_YRGB = 0`: `vop2_scale_factor` returns 0 for
      `SCALE_NONE`.
    - `0x1C30 SCL_CTRL = 0x44`: hor/ver scl mode [1:0]/[5:4] = 0 (none);
      hscl/vscl filter [3:2]/[7:6] = 1 (`VOP2_SCALE_DOWN_BIL`, the
      `hsd/vsd_filter_mode` of Smart0).
    - `SCL_FACTOR_CBR` (`0x1C38`) is not written for RGB; the reset value
      `0x10001000` stays.
29. `0x1CD4 BG_EN = 0`, `0x1CD0 COLOR_KEY_CTRL = 0`.
30. `0x1C20 ACT_INFO = ((720−1)<<16) | (1280−1) = 0x02cf04ff`;
    `0x1C24 DSP_INFO = 0x02cf04ff`.
31. `0x1C28 DSP_ST = 0`: (`dst.y1`<<16) | `dst.x1`, relative to the active
    area. The porches are already in `hact_st`/`vact_st`.
32. `0x1C00 CTRL0 = 0`: `y2r_en`, `r2y_en`, `csc_mode` all 0.
33. `0x1C10 REGION0_CTRL = 0x00000001`: `enable` bit 0, written last.
    Regions 1–3 (`0x1C40`, `0x1C70`, `0x1CA0`) stay 0.
    `CTRL1` (`0x1C04`) is **not written on RK3568**:
    `rk3588_vop2_win_cfg_axi` is skipped for `VOP_VERSION_RK3568`, and the
    reset value `0xd0c0` (axi_yrgb_id=0xc, axi_uv_id=0xd) stays.

**F. Commit and start** (`vop2_crtc_atomic_enable` tail)

34. `0x000 REG_CFG_DONE = 0x00008001`.
35. `0xC00 VP0_DSP_CTRL = 0x0001000f`: release standby, takes effect immediately.
36. Poll `REG_CFG_DONE & 1` until it reads 0. The vendor timeout is 100 ms
    (`vop2_wait_for_fs_by_done_bit_status`); expect at most one frame,
    16.7 ms. Only then verify registers by readback.

The vendor actually commits twice. First mode + `cfg_done` + standby release
in `atomic_enable`; then overlay + window + `cfg_done` in
`atomic_begin`/`atomic_flush`. The single commit above uses the same ordering
rule (all timing configured and committed before the standby release). If
the first frame misbehaves, split it the vendor way: steps 1–21, 34–36, then
22–33, 34, 36.

**Bring-up aids without a framebuffer:**

- `0xC08 VP0_COLOR_BAR_CTRL`: bit 0 = enable, bit 1 = 0 horizontal / 1
  vertical bars (`vop2_crtc_set_color_bar`); needs a cfg_done.
- `0xC2C DSP_BG` set to a colour, e.g. `0x3ff00000` = full red, shows through
  with the window disabled.

---

## 5. SYS block: decode against the snapshot

| Off | Name | Snapshot | Decode / status |
|---|---|---|---|
| 000 | REG_CFG_DONE | `00008000` | bit 15 `GLB_CFG_DONE_EN`=1, VP bits [2:0]=0 (all latched) ✔ |
| 004 | VERSION_INFO | `40158023` | version [31:16] = `0x4015` (read-only; use as a probe) |
| 008 | SYS_AUTO_GATING_CTRL | `00000000` | bit 31 `auto_gating_en`=0 ✔ (`vop2_initial`) |
| 010 | — | `00000ac0` | **unexplained** (no RK3568 name; not written) |
| 024 | SYS_AXI_LUT_CTRL | `e0000010` | bit 0 `lut_dma_en`=0 ✔; bits 4, 29–31 **unexplained** (reset?) |
| 028 | DSP_IF_EN | `00000002` | `hdmi0_en`=1, `hdmi0_mux` [11:10]=0 → VP0 ✔ |
| 02C | DSP_IF_CTRL | `00000000` | ✔ |
| 030 | DSP_IF_POL | `100000b0` | bit 28 imd ✔; bit 7 `hdmi_dclk_pol`=1 ✔; [6:4]=3 → +H +V, DEN high ✔ |
| 034 | SYS_PD_CTRL | `00000000` | RK3568 has no VOP-internal PDs (no `pd_data` in `rk3568_vop`) |
| 040 | WB_CTRL | `70680000` | `axi_yrgb_id` [26:19]=0xd, `axi_uv_id` [31:27]=0xe ✔ (`vop2_initial`); WB disabled |
| 044 | WB_XSCAL_FACTOR | `10000030` | `fifo_throd`=0x30, `scale_x_factor`=0x1000; WB unused, not written here — **unexplained origin** (reset?) |
| 050 | OTP_WIN_EN | `00000001` | RK3566 ✔ |
| 058 | LUT_PORT_SEL | `00000000` | gamma port VP0, wb vp_id 0 (unused) |
| 060 | SYS_STATUS0 | `00e70002` / `02300002` | [31:16] = **VP0 current line** (231 then 560 in the two dumps: counter running). Low `0x0002` **unexplained** |
| 064 | SYS_STATUS1 | `00000002` | VP1 line 0 (standby); low bit 1 **unexplained** |
| 070 | VP0_LINE_FLAG | `62e902e9` | `line_flag_num[0]` [12:0]=745, `[1]` [28:16]=745 ✔; bits 30:29 **unexplained** (also set in VP1/VP2 `0x60000000` → reset value) |
| 080 | SYS0_INT_EN | `00000002` | bit 1 `BUS_ERROR` ✔ (`vop2_axi_irqs_enable`) |
| 08C | (SYS0 raw status, inferred) | `00000024` | bit 5 = `WB_COMPLETE` per `rk3568_vop_axi_intrs`; bit 2 unnamed — **unexplained**/history |
| 090 | SYS1_INT_EN | `00000002` | ✔ |
| 0A0 | VP0_INT_EN | `00000010` | `POST_BUF_EMPTY` ✔ (`vop2_debug_irq_enable`); FS_FIELD off (vblank idle) |
| 0A8 | VP0_INT_STATUS | `00000000` | masked status |
| 0AC | VP0_INT_RAW_STATUS | `000002ef` | sticky raw: bits 0,1,2,3,5,6 named (see §7); bits 7, 9 **unnamed** |
| 0BC / 0CC | VP1 / VP2 raw | `1ed` / `ef` | history; VPs in standby (a released standby VP runs a default timing, comment before `vop2_power_domain_off_by_disabled_vp`) |
| 0DC | — | `0000000f` | RK3588 VP3 range; **unexplained** on RK3568 |

## 6. OVL and VP0: decode against the snapshot

### Overlay (OVL)

| Off | Name | Snapshot | Decode |
|---|---|---|---|
| 600 | OVL_CTRL | `00000000` | `overlay_mode`(b0)=0 RGB; b4 `hdr10_en`=0; b5 `sdr2hdr_path_en`=0; b28 imd set at init and cleared again by `vop2_setup_layer_mixer_for_vp` ✔; [31:30] `ovl_cfg_done_port`=0 |
| 604 | OVL_LAYER_SEL | `54706213` | 4-bit field per layer (3 bits used): L0=3 **Smart0**, L1=1 Cluster1, L2=2 Esmart0, L3=6 Esmart1, L4=0 Cluster0, L5=7 Smart1; L6/L7 (`0x4`,`0x5`) do not exist on RK3568 (`nr_layers` 6) — **reset value kept by the shadow RMW** |
| 608 | OVL_PORT_SEL | `00000755` | PORT0_MUX [3:0]=5, PORT1_MUX [7:4]=5, PORT2_MUX [11:8]=7; win_vp_id Cluster0 [17:16], Cluster1 [19:18], Esmart0 [25:24], Esmart1 [27:26], **Smart0 [29:28]**, Smart1 [31:30] all 0 → VP0 |
| 650–69C | MIX0..MIX4 ×4 | `20`, `00ff0060`, `20`, `74` | see below ✔ |
| 6C0 | HDR0_SRC_COLOR_CTRL | `0` | ✔ (`vop2_setup_alpha`, SDR) |
| 6E0 | VP0_BG_MIX_CTRL | `2a000000` | `bg_dly`=42 ✔ |
| 6E4 / 6E8 | VP1 / VP2 BG_MIX | `26000000` / `28000000` | other VPs, not used |
| 6F0 | CLUSTER_DLY_NUM | `0` | cluster `dly[0]`=0 ✔ |
| 6F8 | SMART_DLY_NUM | `14141414` | Smart0 [23:16]=20 ✔ (`dly={20,47,41}`, index `VOP2_DLY_MODE_DEFAULT`); the other three bytes are also 20 (earlier commits; harmless) |

Mixer words (`union vop2_alpha_ctrl`: b0 color_mode, b1 alpha_mode,
b3:2 blend_mode, b4 alpha_cal_mode, b7:5 factor_mode, b8 alpha_en, b9 swap,
b23:16 glb_alpha). The "transfer" configuration of `vop2_setup_alpha` is
src/dst premultiplied, no source pixel alpha, glb 0xff:

- `SRC_ALPHA = 0x20`: factor ALPHA_ONE.
- `SRC_COLOR = 0x20`: the vendor writes the src_alpha word into SRC_COLOR in
  that loop, so the two match. alpha_en = 0, so the mixer passes through.
- `DST_COLOR = 0x00ff0060`: factor ALPHA_SRC_INVERSE, glb 0xff.
- `DST_ALPHA = 0x74`: blend PER_PIX, NO_SATURATION, factor SRC_INVERSE.

The XRGB "X" byte therefore never takes part in blending in this setup.

### Video port VP0

| Off | Name | Snapshot | Decode |
|---|---|---|---|
| C00 | DSP_CTRL | `0001000f` | out_mode=0xF, pre_dither_down=1, standby=0 ✔ |
| C04 | DUAL_CHANNEL_CTRL | `00000000` | ✔ (VP1/VP2 read `80000000`, see §9) |
| C08 | COLOR_BAR_CTRL | `0` | ✔ |
| C0C | CLK_CTRL | `0` | not used by RK3568 regs |
| C10–C1C | 3D_LUT_CTRL + ? | `210`, `0c830000`, `0e420000`, `08c40000` | `cubic_lut_en`(b0)=0, update(b2)=0; rest **unexplained** |
| C20 | 3D_LUT_MST | `0` | |
| C2C | DSP_BG | `0` | ✔ |
| C30 | PRE_SCAN_HTIMING | `02a90028` | 681<<16 \| 40 ✔ |
| C34/C38 | POST_DSP_H/VACT_INFO | `01040604` / `001902e9` | ✔ |
| C3C | POST_SCL_FACTOR_YRGB | `10001000` | 1:1 ✔ |
| C40 | POST_SCL_CTRL | `0` | ✔ |
| C48/C4C/C50/C54 | timing | `06720028` `01040604` `02ee0005` `001902e9` | ✔ (§4 step 16) |
| C60 | BCSH_CTRL | `00000088` | y2r/r2y csc_mode = 2, enables 0 ✔ |
| C64/C68/C6C | BCSH_BCS/H/COLOR_BAR | `0` | `bcsh_en`=0 ✔ |
| CA0–CB4 | ? | `12844821 21488412 a55a9696 5aa56969 deb77bed ed7bb7de` | look like dither patterns (RK3576 names this range `POST_DITHER_FRC_*`); not written on RK3568 — **unexplained/reset** |

Notes on the `DSP_CTRL` fields above:

- **`out_mode` 0xF (`ROCKCHIP_OUT_MODE_AAAA`)** is the "special outface"
  mode: 30-bit (10 bits per component) parallel output to the encoder.
  `vop2_crtc_setup_output_mode` keeps AAAA only if the VP has
  `VOP_FEATURE_OUTPUT_10BIT` (VP0 yes, VP1/VP2 fall back to P888). The
  rockchip dw-hdmi glue requests AAAA for HDMI; the summary shows `output_mode[f]`.
- **`pre_dither_down_en` = 1** is set by `vop2_dither_setup` for the RGB888 bus
  format, using the ALLEGRO selection; `dither_down_en` = 0.

## 7. Smart0 window: decode against the snapshot

| Off | Name | Snapshot | Decode |
|---|---|---|---|
| 1C00 | CTRL0 | `0` | csc off ✔ |
| 1C04 | CTRL1 | `0000d0c0` | axi_yrgb_id [8:4]=0xc, axi_uv_id [16:12]=0xd, ymirror b31=0 — reset value, not written on RK3568 |
| 1C10 | REGION0_CTRL | `00000001` | enable=1, format 0 = ARGB8888, no swaps ✔ |
| 1C14 | REGION0_YRGB_MST | `00000000` | **IOVA** under the Linux IOMMU → our `FB_PHYS` |
| 1C18 | REGION0_CBR_MST | `0` | |
| 1C1C | REGION0_VIR | `00000500` | 1280 words = 5120 B pitch ✔ |
| 1C20 | ACT_INFO | `02cf04ff` | 1280×720 ✔ |
| 1C24 | DSP_INFO | `02cf04ff` | ✔ |
| 1C28 | DSP_ST | `0` | ✔ |
| 1C30 | SCL_CTRL | `00000044` | filters = BIL, modes = none ✔ |
| 1C34 | SCL_FACTOR_YRGB | `00000000` | `SCALE_NONE` → 0 ✔ (regions 1–3 hold the reset value `0x10001000` = 1.0 in 4.12) |
| 1C38 | SCL_FACTOR_CBR | `10001000` | reset value, not written for RGB |
| 1C3C | SCL_OFFSET | `0` | |
| 1C40/1C70/1CA0 | REGION1..3_CTRL | `0` | areas disabled |
| 1CD0/1CD4 | COLOR_KEY_CTRL / BG_EN | `0` | ✔ |

Pixel layout: DRM XR24 is little-endian `0xXXRRGGBB` per 32-bit word, i.e.
bytes B, G, R, X in memory. VOP2 format 0 with `rb_swap` = 0 reads it
directly; ABGR/XBGR would need `rb_swap` = 1 (`vop2_win_rb_swap`).

### Overlay association for Smart0 (question 3)

- `layer_sel_id` of Smart0 is **3** for VP0/1/2 (`rk3568_vop_win_data`). The
  other IDs: Cluster0 0, Cluster1 1, Esmart0 2, Esmart1 6, Smart1 7.
- Layer *k* is selected by `OVL_LAYER_SEL` [4k+2:4k]. Layer 0 is the bottom of
  the mixer chain; zpos 0 goes to the first layer of the VP. A window and a
  layer must map one to one, so the driver swaps the displaced window into the
  freed layer (`vop2_setup_layer_mixer_for_vp`).
- The VP is chosen by `win_vp_id` in `OVL_PORT_SEL`; Smart0's field is [29:28].
- `PORTn_MUX` = index of the last layer owned by VPn, counted cumulatively
  from VP0; 8 means "no layers"; VP2 is always 7
  (`vop2_calc_bg_ovl_and_port_mux`). The "bg overlay delay" is
  `(nr_mixers(5) − mux)·2`, and `bg_dly = pre_scan_max_dly[3]` (42 for SDR on
  VP0) minus that.
- **Recommended (snapshot-proven):** all six windows on VP0, which is the
  default "one display" `rk3568_vp_plane_mask` policy. Use
  `PORT_SEL = 0x00000755`, `LAYER_SEL = 0x54706213` (Smart0 in layer 0),
  enable only Smart0 and program MIX0..4 as pass-through.
- **Smart0 truly alone on VP0 (derived from the same formulas, NOT verified
  on the board):**
  - Park the other five windows on VP1 (`win_vp_id` = 1).
  - `PORT_SEL = 0x45050750`: VP0 mux 0, VP1 mux 5, VP2 7; Cluster0/1,
    Esmart0/1 and Smart1 → VP1 (fields [17:16], [19:18], [25:24],
    [27:26], [31:30] = 1), Smart0 [29:28] = 0.
  - Layer 0 = Smart0.
  - `bg_dly = 42 − 10 = 32`, so `BG_MIX = 0x20000000` and
    `PRE_SCAN_HTIMING = ((32+639)<<16)|40 = 0x029f0028`.
  - No mixer is in VP0's path. Use only if the six-window variant is
    unwanted.

---

## 7a. Interrupts and "is it scanning?" (question 4)

VP0 interrupt bits are hardware bit positions from `rk3568_vop_intrs`: the
array index is the bit.

| Bit | Name | Meaning |
|---|---|---|
| 0 | FS_INTR | frame start |
| 1 | FS_NEW_INTR | |
| 2 | LINE_FLAG_INTR | VP reached `VP0_LINE_FLAG[12:0]` (745 = end of active video) |
| 3 | LINE_FLAG1_INTR | second line flag [28:16] |
| 4 | POST_BUF_EMPTY_INTR | **underflow**: the window fetch did not keep up, or a bad address |
| 5 | FS_FIELD_INTR | the frame start the vendor uses for vblank (`vop2_crtc_enable_vblank`) |
| 6 | DSP_HOLD_VALID_INTR | standby reached (end of frame after setting standby) |

Registers for VP0:

- `INT_EN 0x0A0` and `INT_CLR 0x0A4`: hiword-masked. To clear bit *b*, write
  `(1<<b) | (1<<(b+16))`; this is write-1-to-clear, as in
  `vop2_read_and_clear_active_vp_irqs`.
- `INT_STATUS 0x0A8`: masked status.
- `INT_RAW_STATUS 0x0AC`: unmasked and sticky until cleared; poll this one.

SYS0: bus error is bit 1 (`0x080`/`0x084`/`0x088`; raw presumably `0x08C`).

**Proof of life without a monitor**, in order of reliability:

1. **Scan-line counter:** `SYS_STATUS0` (`0x060`) bits [31:16] = VP0 current
   line (`vop2_read_vcnt`; VP1/VP2 at `0x064`/`0x068`). The vendor reads it
   twice and accepts the pair when they differ by ≤1, because the counter
   lives in the dclk domain. It must change between reads and stay < 750
   (vtotal). The snapshot's two dumps read 231 and 560.
2. **Commit latch:** `REG_CFG_DONE` bit 0 returns to 0 within one frame after
   writing `0x8001`.
3. **Frame rate:** clear raw bit 0 (or 5), poll `0x0AC` until it reasserts. The
   period must be 1650·750 / 74.25 MHz = **16.667 ms** (60.000 Hz); a line is
   22.22 µs. Bit 2 must assert once per frame as well.
4. **Health:** after a clear, raw bit 4 (`POST_BUF_EMPTY`) and SYS0 bit 1
   (bus error) must stay 0. Otherwise the VOP cannot read `FB_PHYS`: IOMMU
   on, a secure or nonexistent address, or aclk too slow.

IRQ delivery: INTID 180, shared with the VOP IOMMU and all VPs. The vendor
`vop2_isr` walks every VP's `INT_STATUS`.

---

## 8. Flat table: offset → value (base `0xfe040000`)

Write in this order. "=snap" means the steady-state value equals the snapshot.

| # | Off | Register | Value | vs snapshot |
|---|---|---|---|---|
| 1 | 050 | OTP_WIN_EN | `00000001` | =snap |
| 2 | 000 | REG_CFG_DONE | `00008000` | =snap |
| 3 | 008 | SYS_AUTO_GATING_CTRL | `00000000` | =snap |
| 4 | 600 | OVL_CTRL | `10000000` (transient) | final 0 =snap |
| 5 | 030 | DSP_IF_POL | `100000b0` | =snap |
| 6 | 608 | OVL_PORT_SEL | `00000755` | =snap |
| 7 | 604 | OVL_LAYER_SEL | `54706213` | =snap |
| 8 | 084 / 080 | SYS0_INT_CLR / _EN | `00020002` / `00020002` | EN reads `2` =snap |
| 9 | 094 / 090 | SYS1_INT_CLR / _EN | `00020002` / `00020002` | EN reads `2` =snap |
| 10 | 0A4 / 0A0 | VP0_INT_CLR / _EN | `00ff00ff` / `00100010` (EN optional) | EN reads `10` =snap |
| 11 | 028 | DSP_IF_EN | `00000002` | =snap |
| 12 | 02C | DSP_IF_CTRL | `00000000` | =snap |
| 13 | C00 | VP0_DSP_CTRL | `8001000f` (standby) | transient |
| 14 | C04 | VP0_DUAL_CHANNEL_CTRL | `00000000` | =snap |
| 15 | C48 | DSP_HTOTAL_HS_END | `06720028` | =snap |
| 16 | C4C | DSP_HACT_ST_END | `01040604` | =snap |
| 17 | C54 | DSP_VACT_ST_END | `001902e9` | =snap |
| 18 | C50 | DSP_VTOTAL_VS_END | `02ee0005` | =snap |
| 19 | 070 | VP0_LINE_FLAG | RMW: `(old & ~0x1fff1fff) \| 0x02e902e9` | `62e902e9` =snap if reset bits kept |
| 20 | C34 | POST_DSP_HACT_INFO | `01040604` | =snap |
| 21 | C38 | POST_DSP_VACT_INFO | `001902e9` | =snap |
| 22 | C3C | POST_SCL_FACTOR_YRGB | `10001000` | =snap |
| 23 | C40 | POST_SCL_CTRL | `00000000` | =snap |
| 24 | C2C | VP0_DSP_BG | `00000000` | =snap |
| 25 | C08 | VP0_COLOR_BAR_CTRL | `00000000` | =snap |
| 26 | C60 | VP0_BCSH_CTRL | `00000088` | =snap |
| 27 | C6C | VP0_BCSH_COLOR_BAR | `00000000` | =snap |
| 28 | 2010 | SDR2HDR_CTRL | `00000300` | =snap |
| 29 | 2020 | HDR2SDR_CTRL | `00000300` | =snap |
| 30 | 650,654,658,65C | MIX0 SRC_C/DST_C/SRC_A/DST_A | `20`,`00ff0060`,`20`,`74` | =snap |
| 31 | 660…66C | MIX1 | same | =snap |
| 32 | 670…67C | MIX2 | same | =snap |
| 33 | 680…68C | MIX3 | same | =snap |
| 34 | 690…69C | MIX4 | same | =snap |
| 35 | 6C0 | HDR0_SRC_COLOR_CTRL | `00000000` | =snap |
| 36 | 6E0 | VP0_BG_MIX_CTRL | `2a000000` | =snap |
| 37 | C30 | VP0_PRE_SCAN_HTIMING | `02a90028` | =snap |
| 38 | 6F0 | CLUSTER_DLY_NUM | `00000000` | =snap |
| 39 | 6F8 | SMART_DLY_NUM | `14141414` | =snap |
| 40 | 600 | OVL_CTRL | `00000000` | =snap |
| 41 | 1C14 | SMART0_REGION0_YRGB_MST | **`FB_PHYS`** | **differs** (snap 0 = IOVA) |
| 42 | 1C18 | SMART0_REGION0_CBR_MST | `00000000` | =snap |
| 43 | 1C1C | SMART0_REGION0_VIR | `00000500` | =snap |
| 44 | 1C34 | SMART0_REGION0_SCL_FACTOR_YRGB | `00000000` | =snap |
| 45 | 1C30 | SMART0_REGION0_SCL_CTRL | `00000044` | =snap |
| 46 | 1C3C | SMART0_REGION0_SCL_OFFSET | `00000000` | =snap |
| 47 | 1CD4 | SMART0_BG_EN | `00000000` | =snap |
| 48 | 1CD0 | SMART0_COLOR_KEY_CTRL | `00000000` | =snap |
| 49 | 1C20 | SMART0_REGION0_ACT_INFO | `02cf04ff` | =snap |
| 50 | 1C24 | SMART0_REGION0_DSP_INFO | `02cf04ff` | =snap |
| 51 | 1C28 | SMART0_REGION0_DSP_ST | `00000000` | =snap |
| 52 | 1C00 | SMART0_CTRL0 | `00000000` | =snap |
| 53 | 1C10 | SMART0_REGION0_CTRL | `00000001` | =snap |
| 54 | 000 | REG_CFG_DONE | `00008001` | reads `8000` after latch =snap |
| 55 | C00 | VP0_DSP_CTRL | `0001000f` (standby off) | =snap |
| 56 | — | poll `REG_CFG_DONE & 1 == 0`, then §7a checks | | |

Not written, left at reset, and equal to the snapshot:

- `SMART0_CTRL1` `0xd0c0`, `SMART0_REGION0_SCL_FACTOR_CBR` `0x10001000`;
- Smart0 regions 1–3; Cluster/Esmart/Smart1 control registers (all disabled);
- `SYS_AXI_LUT_CTRL`, `WB_*`, `LUT_PORT_SEL`.

**Matches.** Every value in the table equals the snapshot (`vop2 regs` and
`active_regs` sections) except:

- **`0x1C14` YRGB_MST.** Physical framebuffer address here; IOVA `0` in Linux
  because the IOMMU is attached there.
- **Transients.** `OVL_CTRL` `0x10000000`, `DSP_CTRL` `0x8001000f` and
  `REG_CFG_DONE` `0x8001` exist only during bring-up; the final values match.
- **Masked IRQ registers.** Write values carry the mask half; readback shows
  only `[15:0]`.
- **`VP0_LINE_FLAG`.** Matches only if bits 30:29 are preserved by a
  read-modify-write (or written explicitly as `0x62e902e9`).
- **`SYS_STATUS0`.** Read-only line counter; differs on every read by design.

---

## 9. Snapshot values not explained by the vendor RK3568 path

None of these is written by the RK3568 code path for this mode. They are
presumably reset values or leftovers from earlier use, and none should be
needed for a picture. Values that sit inside written registers are preserved
by writing the snapshot word whole.

1. SYS `0x010 = 0x00000ac0`: no RK3568 name in either header.
2. SYS `0x024 SYS_AXI_LUT_CTRL = 0xe0000010`: bits 4, 31:29 (only bit 0 is named).
3. SYS `0x044 WB_XSCAL_FACTOR = 0x10000030`: writeback is unused.
4. `SYS_STATUS0/1` low half `0x0002` (VP0 running, VP1 in standby alike), and
   `0x068 = 0`.
5. `VPn_LINE_FLAG` bits 30:29 (`0x6000xxxx` in all three VPs).
6. `0x08C = 0x24`, `0x09C = 0x4`. These are presumably SYS0/SYS1 raw status:
   bit 5 = WB_COMPLETE, bit 2 unnamed.
7. VP0 raw bits 7 and 9; VP1/VP2 raw bits set while in standby; `0x0DC = 0xf`.
8. VP0 `0xC10 = 0x210` (bits 4, 9) and `0xC14/0xC18/0xC1C`. VP1/VP2 `0xD10/0xE10 = 0x10`.
9. VP0 `0xCA0–0xCB4` dither-like constants. VP1 `0xD80–0xD94` also has
   constants, but they belong to VP1.
10. VP1/VP2 `DUAL_CHANNEL_CTRL` (`0xD04/0xE04`) = `0x80000000`, while VP0's is 0.
    Bit 31 is `edpi_wms_fs` on RK3562; RK3568 does not name it. Write 0 for VP0
    as the snapshot shows.
11. `OVL_LAYER_SEL` [31:24] = `0x54` (non-existent layers 6/7).
12. `SMART_DLY_NUM` bytes for Esmart0/Esmart1/Smart1 = 20 (only Smart0's is
    written in this commit).
13. HDR block: `0x2028` upper half `0x0664`, tables from `0x203C` (HDR2SDR/SDR2HDR
    bypassed; not loaded in this path).
14. Cluster0/1 `0x1008/0x1088/0x1208/0x1288` and `AFBCD_OUTPUT_CTRL = 0x10`:
    disabled windows.

Explained, so not in this list: `OVL_CTRL = 0` despite bit 28 being set at
init; `SMART0_CTRL1 = 0xd0c0`; `SCL_FACTOR_YRGB = 0` with `SCL_CTRL = 0x44`;
`SRC_COLOR == SRC_ALPHA` in the mixers; `VP0_INT_EN = 0x10`; `WB_CTRL`;
`DSP_IF_POL` bit 7. Also SYS_GRF `0x364 = 0xc000`: that is the HDMI glue's DDC
pin masks (`RK3568_HDMI_SDAIN_MSK`/`SCLIN_MSK` in `dw_hdmi-rockchip.c`), not
the VOP. VOP's `grf_dclk_inv` (bit 3) is only used for RGB output.

## 10. Non-obvious requirements, collected

- **RK3566:** `OTP_WIN_EN = 1` before anything else.
- **Auto-gating off:** `SYS_AUTO_GATING_CTRL` bit 31 = 0. This is a vendor
  workaround for an image shift when a window is enabled.
- **No AXI outstanding/QoS writes for RK3568.** The vendor driver has none in
  `rk3568_vop_ctrl`; the per-window AXI ids stay at reset.
- **`REG_CFG_DONE`:** always write bit 15. The VP bits have no write mask on
  RK3568.
- **Standby last:** release standby only after timing is written and
  `cfg_done` is issued; the release is immediate.
- **Stopping the VP:** set standby, wait for `DSP_HOLD_VALID` (raw bit 6),
  only then gate aclk. Otherwise the memory bus may hang (comment in
  `vop2_crtc_atomic_disable`).
- **Interface registers immediate:** set `DSP_IF_POL` bit 28 so the interface
  registers apply at once.
- **Layer/port select immediate at first setup:** set `OVL_CTRL` bit 28 for
  the initial select, then clear it.
- **HDMI clock polarity:** `hdmi_dclk_pol = 1` always for HDMI. Sync polarity
  bit set = positive. This must agree with the dw-hdmi `FC_INVIDCONF`, which
  the snapshot shows as `0xf8`: VSYNC/HSYNC/DE active-high, HDMI mode.
- **Bypasses for an RGB, SDR, 1:1 picture:**
  - BCSH: `bcsh_en` = 0;
  - HDR2SDR/SDR2HDR: bypass bits;
  - gamma LUT: `DSP_LUT_EN` = 0;
  - 3D LUT: bit 0 = 0;
  - post scaler: factor `0x1000`, ctrl 0;
  - window scaler: factor 0 with modes "none".
- **Dither:** `pre_dither_down_en` = 1 with `out_mode` AAAA for the RGB888
  bus; this is vendor behaviour.
- **Readback:** wait for the cfg_done bit to clear before reading back any
  config register; keep a software shadow for read-modify-write.
- **Clock order:** the VOP is clocked by aclk (AXI fetch), hclk (registers)
  and dclk_vp0 (pixel). The vendor enables hclk/aclk before the first
  register access and dclk before the timing commit.
