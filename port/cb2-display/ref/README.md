# Справка, а не код

Исходники, по которым написан драйвер экрана (`minix/drivers/video/fb/arch/aarch64/`).
Сами файлы в репозиторий не кладутся (`.gitignore`), здесь — откуда их взять.

**Вендорское ядро той версии, что стоит на плате** (6.1.115 — ветка
`rk-6.1-rkr5.1` у `armbian/linux-rockchip`), под GPL:

| Файл | Путь в ядре | Что из него взято |
|---|---|---|
| `vendor-rockchip_drm_vop2.c` | `drivers/gpu/drm/rockchip/` | порядок включения VOP2: `vop2_initial`, `vop2_crtc_atomic_enable`, `vop2_win_atomic_update`, задержки, смесители |
| `vendor-rockchip_vop2_reg.c` | там же | поля регистров RK3568 (`rk3568_vop_*`) |
| `vendor-rockchip_vop_reg.h` | там же | смещения регистров `RK3568_*` |
| `vendor-rockchip_drm_vop.h` | там же | имена битов прерываний, режимы вывода |
| `vendor-dw-hdmi.c`, `vendor-dw-hdmi.h` | `drivers/gpu/drm/bridge/synopsys/` | регистры dw-hdmi и порядок `dw_hdmi_setup()`, включение PHY |
| `vendor-dw_hdmi-rockchip.c` | `drivers/gpu/drm/rockchip/` | таблицы PHY Rockchip (`rockchip_mpll_cfg`, `rockchip_cur_ctr`, `rockchip_phy_config`), `GRF_VO_CON1` |
| `vendor-dw_hdmi.h` | `include/drm/bridge/` | структуры таблиц PHY |
| `vendor-rk3568.dtsi` | `arch/arm64/boot/dts/rockchip/` | для сверки узлов |

Такты (`clk-rk3568.c`) и домены питания (`pm_domains.c`) той же версии лежат
в `../../cb2-gmac/ref/` и `../../cb2-usb/ref/`.

    https://raw.githubusercontent.com/armbian/linux-rockchip/rk-6.1-rkr5.1/<путь>

(с виндовой стороны — из WSL `raw.githubusercontent.com` режется).

**NetBSD-current**, под BSD: `sys/dev/ic/dw_hdmi.c`, `dw_hdmi.h`,
`dw_hdmi_phy.c`, `sys/arch/arm/rockchip/rk_dwhdmi.c`, `rk_vop.c` — логика
dw-hdmi и его PHY без вендорских надстроек (с одной ошибкой: у NetBSD
`OUTPUT_SELECT_BYPASS = 2`, плата и вендор говорят 3).

    https://raw.githubusercontent.com/NetBSD/src/trunk/sys/<путь>

Из GPL-файлов берутся номера регистров, разрядность полей и порядок
действий — факты о железе. Текст, структура и код не переносятся. Каждое
записываемое значение сверено не с этими файлами, а со снимком платы
(`../ref-720p-forced.txt`); расшифровка каждого слова VOP2 — `../vop2-recipe.md`.
