# SDM660 Mi Pad 4: графика — Adreno 512, факты и план

Снято 2026-10-17. Источники — sdm660-mainline (`72bc4bb`), разреженный
клон `~/sdm660-linux`:

- `drivers/gpu/drm/msm/adreno/{a5xx_gpu.c,a5xx_gpu.h,a5xx_power.c,a5xx_preempt.c,a5xx_catalog.c,adreno_gpu.c,adreno_gpu.h,adreno_device.c}`;
- `drivers/gpu/drm/msm/{msm_gpu.c,msm_gpu.h,msm_ringbuffer.*,msm_fence.c,msm_iommu.c}`;
- `drivers/gpu/drm/msm/registers/adreno/{a5xx.xml,adreno_common.xml,adreno_pm4.xml}` (MIT, `freedreno_copyright.xml`);
- `drivers/clk/qcom/{gpucc-sdm660.c,gcc-sdm660.c,clk-branch.*,clk-rcg2.c,clk-alpha-pll.c,gdsc.*,reset.c}`;
- `drivers/pmdomain/qcom/rpmpd.c`, `drivers/regulator/{qcom_smd-regulator.c,qcom_spmi-regulator.c}`;
- `drivers/iommu/arm/arm-smmu/{arm-smmu.c,arm-smmu.h,arm-smmu-qcom.c}`, `drivers/iommu/io-pgtable-arm.c`;
- `drivers/firmware/qcom/{qcom_scm.c,qcom_scm-smc.c,qcom_scm.h}`, `drivers/soc/qcom/mdt_loader.c`.

Деревья: `sdm630.dtsi`, `sdm660.dtsi`, `sdm660-xiaomi-clover-common.dtsi`
и собранное `sdm660-xiaomi-clover.dtb` (`~/clover.dts`).

Другие источники:

- Mesa 26.2.4 (`~/mesa-import/mesa-mesa-26.2.4`: `src/freedreno/drm/msm/*`, `src/gallium/drivers/freedreno/*`, `src/freedreno/common/freedreno_devices.py`, `include/drm-uapi/msm_drm.h`);
- linux-firmware: `WHENCE`, `LICENSES/LICENSE.qcom` (gitlab, 2026-10-17; копия в `~/gpu-fw`);
- снимок LineageOS (`adb-lineage/`).

Факты, не код: GPL-текст не переносится, переносятся смещения, биты и
порядок шагов. Пометки: [C] — в коде или дереве, [I] — вывод,
[T] — снято с устройства.

## Итог

- **GPU — Adreno 512, семейство a5xx, `chip id 0x05010200`.** Блок
  `gpu@5000000`; линия SPI 300, она же INTID 332. Линию подтверждает
  LineageOS: `kgsl_3d0_irq` на GIC 332 [T].
- **Mesa 26.2.4 его поддерживает**: gallium `freedreno`, поколение `fd5`,
  запись `GPUId(512)` с двумя SP. Ядро ей нужно с интерфейсом **msm DRM**
  1.x (§8).
- **Со стороны системы Mesa нужна одна служба** — `adreno` с узлом
  `/dev/dri/renderD128`; `libdrm_freedreno` не нужен, у freedreno свой
  слой. Общая часть DRM у `mali` уже есть: буферы, `mmap` через
  `MIOCMMAP`, PRIME, sync file, `cdevfd`.
- **Новое против Mali — четыре вещи:**
  1. **Внешний IOMMU.** Это ARM SMMU v2 `kgsl_smmu`; GPU — SID 0,
     банк CB0, таблицы — LPAE stage 1 (§4).
  2. **Микрокод CP.** Файлы `a530_pm4.fw` и `a530_pfp.fw` из
     linux-firmware, лицензия `LICENSE.qcom`: в репозиторий MEECHO их
     класть нельзя (§5).
  3. **Выход из secure mode через TrustZone.** Подписанный zap-шейдер
     `a512_zap.mdt` из раздела vendor планшета загружается вызовами SCM
     (PAS 13). Для этого у MEECHO теперь есть вызов ядра
     `sys_qcomscm(2)`. На планшете `SECVID_TRUST_CNTL` = 1, то есть GPU в
     secure mode (§5, §13).
  4. **Питание.** Такты GPUCC и два GDSC; уровень VDDMX через RPM.
     Регулятора GFX mainline не трогает вовсе (§2–3).
- **Первый шаг на железе ничего не пишет** (§11, шаг 1). Он читает только
  то, что заведомо тактировано: сначала GCC, потом GPUCC, потом GPU и
  SMMU — и только если GDSC и такты говорят «включено». Вторая часть той
  же загрузки пишет в контроллеры тактов и делает одну запись в GPU —
  в регистр `CP_SCRATCH`. Она включает домены на частоте кварца, читает
  GPU и SMMU и гасит всё обратно.
- **Человек у планшета не нужен.** Круг идёт по ssh через хаб
  (`cycle.sh`), шаг за шагом, и останавливается на первом отказе (§12).

## 1. Узлы дерева [C]

| Узел | Адрес | Что |
|---|---|---|
| `gpu@5000000` | `0x05000000`, 0x40000, `kgsl_3d0_reg_memory` | `qcom,adreno-512.0`, `qcom,adreno` (`sdm660.dtsi:43`), `status = "okay"` у clover. Линия SPI 300, уровень |
| ↳ такты | | `iface` = GCC_GPU_CFG_AHB_CLK, `rbbmtimer` = GPUCC_RBBMTIMER_CLK, `mem` = GCC_BIMC_GFX_CLK, `mem_iface` = GCC_GPU_BIMC_GFX_CLK, `rbcpr` = GPUCC_RBCPR_CLK, `core` = GPUCC_GFX3D_CLK |
| ↳ питание | | `power-domains = <&rpmpd RPMPD_VDDMX>`; `iommus = <&kgsl_smmu 0>`; icc `gfx-mem` (MASTER_OXILI → SLAVE_EBI) |
| ↳ `zap-shader` | | у clover `firmware-name = "a512_zap.mdt"`, `memory-region` → `gpu@f0b00000` |
| `iommu@5040000` (`kgsl_smmu`) | `0x05040000`, 0x10000 | `qcom,sdm630-smmu-v2`, `qcom,adreno-smmu`, `qcom,smmu-v2`. Такты iface/mem/mem_iface, как у GPU. **`power-domains = <&gpucc GPU_GX_GDSC>`**. `#global-interrupts = 2` (SPI 229, 231), контекстные SPI 329, 330, 331, 332, 116, 117, 349, 350 |
| `clock-controller@5065000` (`gpucc`) | `0x05065000`, 0x9038 | `qcom,gpucc-sdm660`. Входы: xo, GCC_GPU_GPLL0_CLK, GCC_GPU_GPLL0_DIV_CLK |
| `clock-controller@100000` (`gcc`) | `0x00100000`, 0x94000 | `qcom,gcc-sdm660` — уже знаком `qcom_soc` |
| `gpu@f0b00000` (reserved) | `0xf0b00000`, 0x2000, `no-map` | регион zap у clover. Базовый `0xfed00000`+0xa00000 из `sdm630.dtsi` clover удаляет. Ядро MEECHO вырезает `no-map` из карты памяти и из линейного отображения (`pre_init.c:461`) |
| `qfprom` → `gpu-speed-bin@41a0` | `0x00780000` + 0x41a2, биты 5..12 | номер бина; отбор частот в `a5xx_catalog.c:126-134` |
| `firmware/scm` | — | `qcom,scm-msm8998`, без тактов |

Номера линий GIC: SPI n — это INTID n + 32. GPU — 332, SMMU global —
261 и 263, контексты CB0 и CB1 — 361 и 362. Последние два — по порядку
CB, `arm-smmu.c:2161-2178`.

LineageOS (downstream 4.19) называет те же блоки иначе [T,
`adb-lineage/dt-tree.txt`]: `qcom,kgsl-3d0@5000000`,
`arm,smmu-kgsl@5040000`, `gfx@5065000`, а также `qcom,kgsl-hyp` (загрузчик
zap) и `regulator-gfx-stub`.

## 2. Такты и домены [C]

### GCC

У трёх ветвей CBCR `halt_check = BRANCH_VOTED` (численно то же, что
`BRANCH_HALT_VOTED`, `clk-branch.h:30-32`). Включение — бит 0 самого
CBCR и проверка CLK_OFF. После выключения статус не проверяется: CBCR
делят с другими мастерами, и такт может остаться включённым.

| Ветвь (номер дерева) | CBCR | Включение | Примечание |
|---|---|---|---|
| GCC_GPU_CFG_AHB_CLK (55) | 0x71004 | bit 0 | **`CLK_IS_CRITICAL`** — Linux её никогда не гасит, значит загрузчик оставляет её включённой [I]. Это шина к регистрам GPUCC, GPU и SMMU |
| GCC_BIMC_GFX_CLK (22) | 0x7106c | bit 0 | память |
| GCC_GPU_BIMC_GFX_CLK (54) | 0x71010 | bit 0 | память |
| GCC_GPU_GPLL0_CLK (56) | голос 0x5200c | **bit 4** | `HALT_DELAY`: статуса нет, после включения ждать 10 мкс |
| GCC_GPU_GPLL0_DIV_CLK (57) | голос 0x5200c | **bit 3** | то же; 300 МГц от GPLL0 600 МГц [I] |

CBCR (`clk-branch.h`): CLK_OFF — бит 31 (только чтение), NOC_FSM — 30:28
(2 = on), FORCE_MEM_CORE_ON — 14, FORCE_MEM_PERIPH_ON — 13, CLK_ENABLE — 0.
«Включён» значит CLK_OFF = 0 или FSM = 2. После записи бита ждать до
200 мкс. Всё это уже умеет `qsoc_clk_on()` / `qsoc_clk_enable()`, и все
пять ветвей уже есть в `sdm660_gcc.h`.

### GPUCC (`0x05065000`)

**PLL.** `gpu_pll0` +0x00, `gpu_pll1` +0x40, тип alpha DEFAULT.

- Регистры: MODE +0x0, L +0x4, ALPHA +0x8, ALPHA_U +0xc, USER_CTL +0x10,
  CONFIG_CTL +0x18, STATUS +0x24.
- MODE: OUTCTRL b0, BYPASSNL b1, RESET_N b2, LOCK_DET b31.
- USER_CTL: ALPHA_EN b24, VCO_SEL 21:20, POST_DIV 11:8.
- Частота = XO·L + XO·alpha/2^40. Alpha 40-битная и учитывается только
  при ALPHA_EN.
- Linux на старте пишет обоим PLL «800 МГц»: L = 0x29, ALPHA = 0xaaaaab00,
  ALPHA_U = 0xaa, CONFIG_CTL = 0x4001055b, VCO = 2. Включение без FSM:
  BYPASSNL, 5 мкс, RESET_N, ждать LOCK_DET (до 1,5 мс), OUTCTRL.

**Корни (RCG).** CMD: UPDATE b0, ROOT_OFF b31. CFG = CMD + 4: SRC_DIV 4:0
(деление (n+1)/2), SRC_SEL 10:8.

| Корень | CMD_RCGR | Источники |
|---|---|---|
| gfx3d_clk_src | 0x1070 | 0 XO, 1 PLL0, 3 PLL1, 5 GPLL0 |
| rbbmtimer_clk_src | 0x10b0 | 0 XO, 5 GPLL0, 6 GPLL0_DIV; всегда 19,2 МГц от XO |
| rbcpr_clk_src | 0x1030 | то же; 19,2 от XO или 50 = GPLL0_DIV/6 |

У gfx3d частоты sdm660 такие: 19,2 (XO) / 160 (÷4) / 266 / 370 / 430 / 465
/ 585 / 588 / 647 / 700 / 750 (÷2). Источник — PLL на удвоенной
частоте; PLL0 и PLL1 сменяют друг друга на каждой смене частоты, ниже
30 МГц источник — XO (`clk-rcg2.c:1322-1416`).

**Ветви.**

| Ветвь | CBCR | Примечание |
|---|---|---|
| gpucc_cxo_clk | 0x1020 | `CLK_IS_CRITICAL` |
| gpucc_gfx3d_clk | 0x1098 | **hwcg — бит 1 того же регистра**: с HW_CTL законно CLK_OFF = 1 у простаивающего GPU. Он же `cxcs` у GX |
| gpucc_rbbmtimer_clk | 0x10d0 | |
| gpucc_rbcpr_clk | 0x1054 | |

**Домены (GDSC).** Биты GDSCR: PWR_ON b31, EN_REST_WAIT 23:20, EN_FEW_WAIT
19:16, CLK_DIS_WAIT 15:12, RETAIN_FF b11, SW_OVERRIDE b2, HW_CONTROL b1,
SW_COLLAPSE b0.

| | GPU_CX_GDSC | GPU_GX_GDSC |
|---|---|---|
| GDSCR | 0x1004 | 0x1094 |
| Статус | **`gds_hw_ctrl` 0x1008, бит 31** (не сам GDSCR) | GDSCR, бит 31 |
| Флаги | VOTABLE | CLAMP_IO, SW_RESET, AON_RESET, NO_RET_PERIPH; родитель — CX |
| Прочее | — | clamp_io_ctrl 0x130 (CLAMP_IO b0, GMEM_RESET b4), BCR GPU_GX 0x1090 |

Другие BCR: GPU_CX 0x1000, RBCPR 0x1050, SPDM 0x10e0. Сброс — бит 0:
установить, прочитать, 1 мкс, снять.

**Включение в Linux (`gdsc.c`).**

1. Init: в GDSCR под маской `0x00fff006` записать `0x00282000`. Это
   HW_CONTROL = 0, SW_OVERRIDE = 0 и ожидания 2/8/2.
2. CX: снять SW_COLLAPSE, 1 мкс, ждать 0x1008 b31 до 2 мс.
3. GX:
   1. BCR 0x1090: установить, 1 мкс, снять.
   2. clamp 0x130: импульс b4, затем b0 = 0.
   3. Снять SW_COLLAPSE.
   4. Ждать 0x1094 b31 до 2 мс.
   5. В 0x1098 поставить FORCE_MEM_CORE_ON (b14).
4. Выключение GX — обратно: снять b14, SW_COLLAPSE, ждать off, clamp b0 = 1.

### Порядок чтения в пробе [I]

На этой SoC чтение блока без такта вешает шину (`qcom_soc.c`, урок
`sdmmc` и сенсора). Поэтому каждый следующий блок читается, только если
предыдущий сказал «можно»:

1. **GCC** доступен всегда. Прочитать CBCR 0x71004 и убедиться, что
   CFG_AHB включён. Без него к GPUCC не обращаться.
2. **GPUCC**: 0x1020, 0x1008 (CX), 0x1094 (GX), 0x1098, 0x10d0, 0x1054,
   0x1070/0x1074, оба PLL. Нужно ли CFG_AHB для GPUCC — в Linux не сказано
   [I]: держаться безопасного предположения.
3. **GPU и SMMU** — только при GX on, CX on, CFG_AHB on, BIMC_GFX и
   GPU_BIMC_GFX on, а для GPU ещё и gfx3d on. SMMU тоже сидит в GX
   (`power-domains`).

## 3. Питание [C/I]

- **В mainline GPU голосует только VDDMX** через rpmpd: `opp-level` на
  каждую частоту, от MIN_SVS на 19,2 и 160 МГц до NOM на 588
  (`sdm660.dtsi:56-145`).
- **У GDSC нет `supply`, у GPU нет `vdd`/`vddcx`**
  (`msm_gpu.c:26-48` — optional). Рельс GFX mainline не трогает. По
  этой причине выше 588 МГц частоты закомментированы:
  > «lacking support for the GPU regulator» (`sdm660.dtsi:64`)
- **[I] GX на SDM660 питается от MX.** Видимо, через встроенный LDO
  GFX в режиме BHS. Иначе голос VDDMX за частоту GPU не имел бы смысла,
  а с ним mainline держит 588 МГц.
  - LineageOS держит «регулятор GFX» заглушкой (`regulator-gfx-stub`)
    [T].
  - Блок CPR GFX (такт `rbcpr`) mainline не трогает.
  - **Проверено на планшете** (§13): GX GDSC поднимается за 12 мкс при
    том MX, что оставил загрузчик. Механизм это не доказывает, но вывод
    не опровергнут.
- **SMPS PM660L** (`qcom_smd-regulator.c:897-914`,
  `qcom_spmi-regulator.c:2083`):
  - s3 (вместе с s4) — RWCX, то есть VDDCX; s5 — RWMX, то есть VDDMX;
    s1 и s2 — SMPB.
  - В SPMI: s1 0x1400, s2 0x1700, s3 0x1a00, s4 0x1d00, s5 0x2000.
  - Отдельного SMPS «GFX» в таблицах нет.
- **RPM** (`rpmpd.c`) — тот же транспорт, что у `qcom_rpm` (hw-rpm.md):
  - типы rwcx 0x78637772, rwmx 0x786d7772;
  - ключ уровня `vlvl` 0x6c766c76;
  - уровни: RET 16, MIN_SVS 48, LOW_SVS 64, SVS 128, SVS_PLUS 192,
    NOM 256, NOM_PLUS 320, TURBO 384;
  - active set 0, sleep set 1.
  Первым шагам голос не нужен: XO и 160 МГц — это MIN_SVS, а MX при
  горящем экране выше [I].
- **Частоты по бину** (`a5xx_catalog.c:126-134`, `sdm660.dtsi`):
  - бины 135/146/157 и 0 → до 588 МГц;
  - 122 → 465;
  - 90 (SDM636) → 430;
  - прочие → 370.
- **Ловушка Wi-Fi-модели** (`sda660-…`): совместимость `qcom,sda660`
  ведёт Linux по старому пути разбора бина. Для LTE (`sdm660`) путь верный.
- **Пропускная способность BIMC** голосуется через RPM (icc). Без
  голоса память идёт на той частоте, которую держат другие (экран);
  это медленно, но работает [I].

## 4. SMMU `kgsl_smmu` [C]

**Привязка.** `qcom_adreno_smmu_v2_impl` (`arm-smmu-qcom.c:650-681`):

- GPU — **SID 0** и всегда **CB0**; остальным достаются CB с 1-го.
- **TTBR1 на sdm630/660 запрещён** (`:328-338`), поэтому таблиц «на
  процесс» у a5xx в Linux нет: одно общее пространство на всех.
  `CP_SMMU_TABLE_UPDATE` (opcode 0x53) драйвер не шлёт.
- TZ держит последние банки за собой: при NUMCB = 5 доступны CB0 и CB1
  (`:541-552`).

**Глобальные регистры** (`arm-smmu.h`):

- sCR0 0x0: CLIENTPD b0, GFRE b1, GFIE b2, USFCFG b10, VMIDPNE b11,
  PTM b12, FB b13, BSU 15:14.
- ID0 0x20: S1TS b30, SMS b27, NUMSMRG 7:0.
- ID1 0x24: PAGESIZE b31, NUMPAGENDXB 30:28, NUMCB 7:0.
- ID2 0x28: UBS 11:8, OAS 7:4, IAS 3:0.
- sGFSR 0x48; TLBIALLNSNH 0x68; sTLBGSYNC 0x70 / sTLBGSTATUS 0x74.
- SMR(n) 0x800+4n: VALID b31, MASK 31:16, ID 15:0.
- S2CR(n) 0xc00+4n: TYPE 17:16 (0 — трансляция, 1 — bypass, 2 — fault),
  CBNDX 7:0.
- GR1: CBAR(n) 0x1000+4n, CBA2R(n) 0x1800+4n (VA64 b0).

**Страницы.** Страница n — base + (n << pgshift); pgshift = 12 или 16 по
ID1.PAGESIZE. CB n — страница NUMPAGE + n, где NUMPAGE =
2^(NUMPAGENDXB+1). При 4К-страницах это, скорее всего, 0x05048000 +
n·0x1000 [I] — проба сверит по ID1.

**Банк контекста:**

- SCTLR 0x0: M b0, TRE b1, AFE b2, CFRE b5, CFIE b6, CFCFG b7, HUPCF b8,
  ASIDPNE b12.
- RESUME 0x8; TCR2 0x10; TTBR0 0x20 (64 бита, ASID в 63:48); TCR 0x30.
- MAIR0 0x38, MAIR1 0x3c.
- FSR 0x58 (TF b1, PF b3, EF b4, SS b30, MULTI b31; сброс — записью
  прочитанного); FAR 0x60; FSYNR0 0x68.
- TLBIASID 0x610, TLBSYNC 0x7f0, TLBSTATUS 0x7f4.

**Как Linux ставит CB0** (`arm-smmu.c:519-806`, `io-pgtable-arm.c`):

- CBA2R = VA64. CBAR = 0x1f300: S1 и bypass S2, BPSHCFG 3, MEMATTR 0xf.
- TCR: TG0 4К, обход некогерентный (OS/NC/NC), T0SZ = 64 − ias, EPD1.
  TCR2: PASIZE по oas, SEP = 7, AS.
- TTBR0 = pgd, ASID 0.
- MAIR0 = **0xf404ff44**: 0 — NC, 1 — WBRWA, 2 — Device, 3 — 0xf4.
- SCTLR = 0x11e7 со stall (в Linux stall включает `msm_iommu_gpu_new`)
  или 0x1167 без него.
- ias = ID2.UBS, oas = ID2.IAS. Начало VA GPU — 16 МиБ.

**LPAE stage 1, 4К** — тот же формат, что у таблиц процессора:

- лист уровня 3 — тип 3, блок — тип 1;
- AF b10, nG b11;
- AP: b6 — непривилегированный доступ, b7 — только чтение;
- SH: IS для кэшируемого, OS для прочего;
- AttrIndx 4:2; XN b53/54.

У Mali в MEECHO иначе: лист там — тип 1 на любом уровне.

**Сбой.** Прочитать FSR/FSYNR0/FAR и сбросить FSR. При SS = 1 записать
RESUME: 1 — TERMINATE (так делает Adreno) или 0 — повтор. Adreno ещё и
читает `CP_SCRATCH_REG(4..7)` и на 500 мс выключает stall.

**Что оставляет загрузчик** — неизвестно: sCR0.CLIENTPD, SMR/S2CR для
SID 0, SCTLR у CB0. Прочитать **до всяких записей** (шаг 1).

**MEECHO [I].** Таблицы лежат в памяти службы: страница таблицы — через
`sys_umap`, как у `mali_mmu.c`. Обход некогерентный, поэтому после
записи дескрипторов нужен `CACHE_CLEAN` и только потом TLBI.

Изоляцию клиентов MEECHO может держать лучше Linux: служба сама решает,
когда GPU свободен. Перед заданием другого клиента она дожидается
простоя, меняет TTBR0 и ASID у CB0 и делает TLBIASID и sync. Такое
переключение дорогое, но верное. `CP_SMMU_TABLE_UPDATE` из кольца —
позже: без TTBR1 общим буферам ядра (кольцо, memptrs) нужен один адрес во
всех таблицах.

## 5. Микрокод и zap-шейдер [C]

### Микрокод CP

- Файлы `a530_pm4.fw` и `a530_pfp.fw` (PFP 1.87.01), берутся с префиксом
  `qcom/` (`adreno_gpu.c:526-652`).
- Первый dword файла пропускается. Буфер — GPU-only, WC, только чтение
  для GPU. Адреса пишутся в `CP_ME_INSTR_BASE_LO/HI` (0x0838) и
  `CP_PFP_INSTR_BASE_LO/HI` (0x0835).
- Версия проверяется только у PFP, по словам 1 и 3 файла: `(w1 & 0xf) ==
  0xa && (w3 & 0xf) >= 1` → `has_whereami`. Без этого — одно кольцо и
  rptr из регистра.
- **Лицензия** — `LICENSE.qcom` + `NOTICE.qcom` (`WHENCE:7021-7023`):
  - можно распространять **в двоичном виде, только для платформ на
    чипах Qualcomm**, вместе с текстом лицензии;
  - нельзя реверсить;
  - нельзя «subject the Materials to any third party license terms (e.g.,
    open source license terms)».

  Отсюда: **в дерево MEECHO (BSD) не класть.** Сборка берёт файлы из
  linux-firmware локально (`~/gpu-fw`, скрипт скачивания) и кладёт в
  образ с текстом лицензии. Такой образ — локальный; публиковать его
  можно только с лицензией рядом.

### Zap-шейдер и TrustZone

**Зачем.** GPU выходит из сброса в secure mode. Выход — `CP_SET_SECURE_MODE`
(0x66) с аргументом 0 из кольца, и CP исполняет его, только если TZ
загрузил и проверил zap. Запасной путь Linux — запись
`RBBM_SECVID_TRUST_CNTL` (dword 0xf400) = 0 с процессора, но только при
`-ENODEV` (узла `zap-shader` нет). Комментарий Linux: если TZ закрыл
регистр, «a permissions violation will soon follow»
(`a5xx_gpu.c:976-1003`). На clover узел есть, значит Linux идёт через zap.
[I] Для MEECHO zap обязателен, пока проба не покажет обратного.

**Порядок загрузки** (`adreno_gpu.c:30-167`, `mdt_loader.c`):

1. Файл `a512_zap.mdt` (+ `.b00`, `.b01`, …) кладётся в регион
   `0xf0b00000`, 8 КиБ.
2. **PAS_INIT_IMAGE**(13, метаданные). Метаданные — ELF-заголовок
   (phdr[0].p_filesz) и hash-сегмент в физически непрерывном,
   выровненном на 4К, некэшируемом буфере.
3. **PAS_MEM_SETUP**(13, адрес, размер) — только если у сегмента стоит
   RELOCATABLE (бит 27).
4. Сегменты копируются по p_paddr; хвост memsz обнуляется.
5. **PAS_AUTH_AND_RESET**(13).
6. В кольцо — `CP_SET_SECURE_MODE` 0.
7. На повторном init образ не перегружается:
   **SET_REMOTE_STATE**(0, 13).

**SCM — SMCCC** (`qcom_scm-smc.c`):

- x0 = FID, x1 = arginfo, x2..x5 — аргументы; ответ x0 — статус, x1..x3 —
  результаты. Отказ — и ненулевой x0, и ненулевой x1.
- Пока x0 = 1 (INTERRUPTED), повторять с x0 = 1. На −12 (EBUSY) — до 20
  повторов через 30 мс.
- Соглашение (ARM_64 / ARM_32 / legacy) выясняется пробой
  IS_CALL_AVAIL.

| Вызов | FID | arginfo |
|---|---|---|
| IS_CALL_AVAIL (6/1) | 0xC2000601, fast | 0x1; аргумент — FID без owner-битов |
| PAS_IS_SUPPORTED (2/7) | 0x42000207 | 0x1 (id) |
| PAS_INIT_IMAGE (2/1) | 0x42000201 | 0x82 (id; физ. адрес, RW) |
| PAS_MEM_SETUP (2/2) | 0x42000202 | 0x3 |
| PAS_AUTH_AND_RESET (2/5) | 0x42000205 | 0x1 |
| SET_REMOTE_STATE (1/0xa) | 0x4200010a | 0x2 |

Ошибки: −1 ERROR, −2 EINVAL_ARG, −3 EINVAL_ADDR, −4 EOPNOTSUPP, −5 ENOMEM.

**Откуда брать образ.** В linux-firmware zap для a512/sdm660 нет: есть
`a530_zap` для apq8096 и новее. Образ подписан производителем.

- [I] На планшете он в `/vendor/firmware/a512_zap.mdt` и `.b00..b02`.
  Может быть и в разделе modem (`/firmware/image`) — проверить
  `adb shell ls` под LineageOS, без root.
- Взять с планшета (`adb pull`, если SELinux пустит) или из образа vendor
  прошивки LineageOS/Xiaomi.
- **В репозиторий не класть**, в опубликованные образы — тоже. Только
  локальный комплект в `D:\minix\clover-flash`.
- Перемещаемость образа и его размер против 8 КиБ региона — проверить по
  самому файлу (заголовок ELF).

**MEECHO [I].** Нужен **новый вызов ядра**: у процесса на EL0 SMC нет, а у
ядра он есть (`psci.c`). Сквозной «SMC с любыми аргументами» не годится:
PAS_INIT_IMAGE и MEM_SETUP получают **физические адреса**, и TZ может
запереть ими чужую память. Значит вызов должен быть узким:

- белый список FID — из `system.conf` службы;
- адреса — виртуальные в пространстве вызывающего, ядро переводит их
  само (как `sys_cachectl`);
- для MEM_SETUP — только регион `no-map` из дерева с именем
  `zap-shader` / `memory-region`.

## 6. Запуск GPU [C]

**Порядок Linux (`adreno_load_gpu`, `a5xx_pm_resume`, `a5xx_hw_init`).**
Для A512 в `a5xx_power.c` нет ни одной записи: GPMU, LM и RBCCU — только у
a530/a540.

1. Микрокод в буферы GPU — **до** включения питания.
2. Такты: core через OPP, `rbbmtimer` = 19,2 МГц, затем все такты узла.
3. `RBBM_CLOCK_CNTL` (0x0067) = 0x55, затем HWCG.
4. `hw_init` — по порядку (`a5xx_gpu.c:695-1009`, полный список с
   значениями — отчёт этого разбора, не здесь):
   - VBIF: QOS_ARB (0x3049) = 3, GATE_OFF_WRREQ (0x302a) = 9 (a512);
   - RBBM: AHB_CNTL0/1/2, INTERFACE_HANG_INT_CNTL (0x002f) = 0x4000ffff,
     счётчики;
   - UCHE: CACHE_WAYS = 2; TRAP_BASE / WRITE_THRU_BASE (0x0e89/0x0e87,
     64 бита) = 0x0001ffffffff0000; GMEM_RANGE (0x0e8b/0x0e8d) =
     0x100000 … 0x143fff;
   - CP: MEQ/MERCIU/ROQ (ветка «не 505/506/508/510»), PC/RB/VPC DBG_ECO,
     UCHE_MODE_CNTL, CP_CHICKEN_DBG;
   - HWCG: таблица `a512_hwcg`, 56 записей (её значения — в
     `a5xx_gpu.c:387-443`); RBBM_CLOCK_CNTL = 0xaaa8aa00,
     RBBM_ISDB_CNT = 0x182;
   - highest bank bit 14: TPL1_MODE_CNTL = 0x80, RB_MODE_CNTL = 2,
     UCHE_DBG_ECO_CNTL_2 = 1;
   - **CP_PROTECT**: CNTL (0x08a0) = 7, 18 записей
     `(1<<30)|(1<<29)|(log2(len)<<24)|(reg<<2)`. База обязана делиться на
     длину — железо маскирует молча. Последняя (0x10000, 0x8000) —
     окно SMMU;
   - SECVID_TSB_* = 0; все `*_ADDR_MODE_CNTL` = 1 (64-битный режим);
   - квирк LMLOADKILL (у A512 он есть).
5. `CP_ME/PFP_INSTR_BASE`; `CP_RB_BASE` (0x0800); **`CP_RB_CNTL` (0x0802) =
   0x0800020c**: BUFSZ 12 (32 КиБ), BLKSZ 2, NO_UPDATE. Тень rptr в
   железе выключена, вместо неё `CP_WHERE_AM_I`. `CP_RB_RPTR_ADDR` —
   буфер тени.
6. `RBBM_INT_0_MASK` (0x0038) = **0x1190037e**.
7. **`CP_PFP_ME_CNTL` (0x0832) = 0 — CP стартует.**
8. **`CP_ME_INIT`** (0x48, 8 слов): 0x2f, 3, 0x20000000, 0, 0, 0 (обходов
   микрокода у A512 нет), 0, 0; заголовок 0x70c80008. Затем WHERE_AM_I,
   запись WPTR и ожидание простоя:
   - rptr == wptr;
   - `RBBM_STATUS` (0x04f5) без бита 0 равен 0;
   - нет `MISC_HANG_DETECT` (бит 23 `RBBM_INT_0_STATUS`, 0x04e1);
   - предел — 1 с.
9. zap → `CP_SET_SECURE_MODE` 0 → простой.
10. Вытеснение — четыре кольца при whereami. Первому кругу не нужно:
    одно кольцо, `CP_CONTEXT_SWITCH_SMMU_INFO` = 0.

**Пакеты** (`adreno_pm4.xml:240-245`):

- чётность `P(v) = (0x9669 >> (XOR восьми полубайтов v)) & 1`;
- **PKT4** = 0x40000000 | cnt[6:0] | P(cnt)<<7 | reg[17:0]<<8 | P(reg)<<27,
  не больше 127 слов;
- **PKT7** = 0x70000000 | cnt[14:0] | P(cnt)<<15 | op[6:0]<<16 | P(op)<<23.

Эталоны для стенда:

| Пакет | Заголовок |
|---|---|
| SET_PROTECTED_MODE,1 | 0x70df0001 |
| EVENT_WRITE,4 | 0x70460004 |
| INDIRECT_BUFFER_PFE,3 | 0x70bf8003 |
| WHERE_AM_I,2 | 0x70620002 |
| PKT4(0x0b7a,1) | 0x480b7a01 |

**Регистры для пробы** — все в dword; байтовое смещение ×4:

- `RBBM_STATUS` 0x04f5;
- `RBBM_ALWAYSON_COUNTER_LO/HI` 0x04d2/3 — 19,2 МГц, тот же, что
  MSM_PARAM_TIMESTAMP;
- `VBIF_VERSION` 0x3000;
- `RBBM_SECVID_TRUST_CNTL` 0xf400;
- `CP_SCRATCH_REG(0..7)` 0x0b78;
- `RBBM_INT_0_STATUS` 0x04e1;
- `CP_PFP_ME_CNTL` 0x0832.

**Регистра chip id у a5xx в XML нет** — Linux берёт его из
`compatible`. У a3xx и a4xx по 0x0000 лежит `RBBM_HW_VERSION`; у a5xx
0x0000–0x0002 без имени, но входят в список дампа Linux
(`a5xx_gpu.c:1310`), то есть их чтение безопасно. [I] Проба читает и
печатает их: возможно, это и есть номер железа.

## 7. Отправка, забор, прерывания [C]

**Задание** (`a5xx_submit`, одно кольцо, без вытеснения):

1. `CP_SET_PROTECTED_MODE` 0.
2. PKT4 `CP_CONTEXT_SWITCH_SAVE_ADDR`.
3. PROT 1.
4. На каждый буфер команд: `CP_INDIRECT_BUFFER_PFE` (0x3f) {iova_lo,
   iova_hi, dwords}; каждые 32 — WHERE_AM_I.
5. PKT4 `CP_SCRATCH_REG(2)` = seqno.
6. **`CP_EVENT_WRITE` (0x46) {0x80000004 = CACHE_FLUSH_TS | IRQ, lo, hi,
   seqno}** в `memptrs + 4`. Готово, когда `(int32)(fence − seqno) >= 0`.

**Прерывания** — маска 0x1190037e:

| Бит | Что |
|---|---|
| 1–6 | ошибки RBBM: AHB_ERROR, TRANSFER_TIMEOUT, ME/PFP/ETS_MS_TIMEOUT, ATB_ASYNC_OVERFLOW |
| 8 | CP_SW |
| 9 | CP_HW_ERROR |
| 20 | **CP_CACHE_FLUSH_TS** |
| 23 | MISC_HANG_DETECT |
| 24 | UCHE_OOB |
| 28 | GPMU_DROOP |

Обработчик:

- прочитать `RBBM_INT_0_STATUS` и записать в `RBBM_INT_CLEAR_CMD` (0x0037)
  всё **без бита 1**: AHB_ERROR снимается только после источника, иначе
  шторм;
- AHB: `RBBM_AHB_ERROR_STATUS` (0x04f4), `RBBM_AHB_CMD` (0x0096) = 1<<4;
- CP_HW_ERROR → `CP_INTERRUPT_STATUS` (0x0b18): opcode, HW_FAULT 0x0b1a,
  PROTECT 0x0b1c (бит 24 — запись), AHB 0x0b1b;
- HANG: если `RBBM_STATUS3` (0x0530) бит 24 — SMMU stalled, ждать
  обработчика SMMU;
- забор: hangcheck Linux — 500 мс без движения fence.

**Восстановление.** `RBBM_SW_RESET_CMD` (0x0043) = 1, прочитать, = 0. Затем
suspend/resume и полный `hw_init`; zap — через SET_REMOTE_STATE.

**Выключение** (`a5xx_pm_suspend`): `VBIF_XIN_HALT_CTRL0` (0x3080) = 0xf,
ждать `CTRL1` (0x3081) & 0xf == 0xf, затем CTRL0 = 0. Потом такты и GDSC.

## 8. Интерфейс для Mesa: msm DRM [C]

Mesa 26.2.4, `src/freedreno/drm/msm/*`, `src/gallium/drivers/freedreno/*`.

**Как Mesa его находит.** `drmGetVersion` → имя **`"msm"`**, major 1.
Минор включает возможности (`freedreno_drmif.h:168-179`):

| Минор | Возможность |
|---|---|
| 1 | madvise, неограниченное число cmds |
| 2 | fence fd, memory fd |
| 3 | GMEM_BASE, очереди, iova |
| **4** | **softpin** — путь `msm_ringbuffer_sp.c`: без relocs, iova от ядра |
| 5 | FAULTS |
| 7 | SUSPENDS |
| 8 | проба `MSM_BO_CACHED_COHERENT` (неудача — нормально) |
| 9 | VA_SIZE |

[I] Отвечать миноров ровно столько, сколько служба умеет. Начать с
softpin и FAULTS — 1.5 или 1.6.

**Вызовы.**

- `GET_PARAM`: GPU_ID, GMEM_SIZE, CHIP_ID, GMEM_BASE, MAX_FREQ,
  TIMESTAMP, PRIORITIES (NR_RINGS), FAULTS, SUSPENDS, VA_SIZE,
  UCHE_TRAP_BASE.
- `GEM_NEW` (флаги WC / CACHED_COHERENT / SCANOUT / GPU_READONLY).
- `GEM_INFO`: GET_OFFSET (для `mmap`), GET_IOVA, SET_NAME,
  SET/GET_METADATA.
- `GEM_CPU_PREP`/`CPU_FINI`, `GEM_MADVISE`.
- `GEM_SUBMIT`: cmds типа BUF, bos с READ/WRITE, флаги FENCE_FD_IN/OUT,
  NO_IMPLICIT.
- `WAIT_FENCE`, `SUBMITQUEUE_NEW/CLOSE/QUERY`, `SET_PARAM` (SYSPROF).
- Общие DRM: `GEM_CLOSE`, `PRIME_*`, `GET_CAP` (SYNCOBJ).

**Значения для A512** (как у Linux, `adreno_get_param`):

| Параметр | Значение |
|---|---|
| GPU_ID | 512 |
| CHIP_ID | 0x05010200 |
| GMEM_SIZE | 0x44000 (272 КиБ) |
| GMEM_BASE | 0x100000 |
| TIMESTAMP | ALWAYSON_COUNTER |
| PRIORITIES | 3 при одном кольце |
| MAX_FREQ | по бину |
| VA_START/VA_SIZE | у Linux на a5xx `EINVAL` (нет таблиц на процесс); у MEECHO таблицы свои, можно отвечать |
| HIGHEST_BANK_BIT | 14 |
| UCHE_TRAP_BASE | 0x0001ffffffff0000 |

**Буферы и кэши.** Mesa просит `MSM_BO_WC` для всех буферов, кроме
CACHED_COHERENT — а его у a5xx нет. У MEECHO своих WC-отображений для
процесса нет, поэтому путь тот же, что у `mali`: кэшируемая память и
`CACHE_CLEAN_INVALIDATE` вокруг задания. Цена у Mali — 0,53 мс на МиБ
(CLAUDE.md, 11.6). [I] Для a5xx то же самое; лучше было бы
WC-отображение у VM.

**Заголовок `msm_drm.h` — MIT**: Mesa держит копию в
`include/drm-uapi/msm_drm.h`. В `/usr/include/gfxcompat` его можно класть.

**Сборка Mesa.** `mesa-import.sh` сейчас настраивает
`-Dgallium-drivers=panfrost`. Freedreno — это
`-Dgallium-drivers=panfrost,freedreno` (`freedreno-kmds=msm` по
умолчанию), а также повтор `mesa-reachover.py`: `dist/` содержит только
прочитанные сборкой файлы. LLVM freedreno не нужен (ir3 — на NIR). В
gallium freedreno есть C++ (fd6), C++ тулчейн есть
(`~/xtools-aarch64-cxx`). Один `libgallium` с обоими драйверами — одна и
та же программа на CB2 и на планшете.
`pipe_loader` выбирает драйвер по имени `drmGetVersion`.

**Стенды Mesa без железа** [C]:

- `FD_RD_DUMP` пишет поток команд в файл `.rd`; хостовый `cffdump` Mesa
  (`-Dtools=freedreno`, нужен libxml2) его разбирает;
- `src/freedreno/drm-shim` — поддельное msm-ядро для Linux через
  LD_PRELOAD. MEECHO его не возьмёт (статика), но то, что он отвечает, —
  эталон ответов служебного «виртуального» режима (§11).

## 9. Что уже есть в MEECHO

### Переиспользуется почти как есть

- **DRM-часть `mali`** — `minix/drivers/video/mali/mali_drm.c`, 2004
  строки. GPU-независимая половина:
  - контексты по `open` с `CDEV_CLONED`;
  - каркас ioctl: `calldef`, правило размеров DRM, `EDONTREPLY`;
  - `VERSION` (строки — параметр), `GET_CAP`, `GEM_CLOSE`;
  - заборы и syncobj со всеми `SYNCOBJ_*`;
  - объекты дескрипторов: dma-buf, sync file, syncobj — minor с 256 через
    `cdevfd_new()` и `chardriver_open_minor()`;
  - PRIME, `MERGE`/`FILE_INFO` у sync file, `SYNC`/`EXPORT`/`IMPORT` у
    dma-buf;
  - ожидания со сроком, `select`;
  - `mmap` через `MIOCMMAP`.

  Из `mali_bo.c`: страничная память службы с `sys_umap` на страницу,
  отдача клиенту `vm_remap(…, sef_self(), …)`, кэш кусками по 128 КиБ,
  `bo_new_phys` и `SCANOUT_IMPORT` (буферы `fb` через DS — на clover `fb`
  публикует их так же).

  **Предложение:** вынести это в `minix/lib/libdrmdriver`, а `mali` и
  `adreno` сделать двумя потребителями (шаг 7). Хирургия на работающем
  драйвере CB2: стенд `port/test/mali/` (модель, `drmtest`, `primetest`,
  `mutate.sh`) обязан пройти до и после.
- **Стенды по образцу `port/test/mali/`.** Настоящие файлы службы
  собираются хостовым `gcc`. Обращения к регистрам перехватывает
  `shim/regs-hook.h` (`-include`), и модель исполняет каждую запись.
  Деревья — настоящие DTB. `mutate.sh` вносит правдоподобные ошибки.
- **`qcom_soc`** (`minix/drivers/hid/qcom_touch/qcom_soc.c`, через `.PATH`
  его уже берут `dwc3_ecm` и `random`): GCC по compatible и
  `qsoc_clk_on`/`qsoc_gdsc_on`/`qsoc_clk_enable`. Все пять ветвей GCC для
  GPU уже есть в `sdm660_gcc.h`. **GPUCC нет нигде** —
  `gen-sdm660-tables.py` надо научить `gpucc-sdm660.c`.
- **`qcom_rpm`** (GLINK к RPM): уровни RWCX/RWMX в таблице есть, но API для
  клиентов нет. Голос VDDMX для частот выше MIN_SVS — новый запрос к
  службе (шаг 10).
- **`fb`** на clover — `simple-framebuffer` и буферы в DS. `glcube`
  копирует кадр в `/dev/fb0` так же, как на CB2.
- **libdrm**: под `__minix` `DRM_MAJOR` 18 и узел `renderD128`. Служба
  отвечает именем `"msm"`, и `pipe_loader` Mesa выбирает freedreno. На
  clover Mali нет — узел свободен. `msm_drm.h` в
  `external/mit/libdrm/dist/include/drm` старый (308 строк); положить
  свежий из Mesa рядом с `panfrost_drm.h`.

### Нет вовсе

- **Кода ARM SMMU** (`drivers/iommu/` — только `amddev` для x86).
- **SMC для служб**: ядро зовёт SMC только для PSCI (`psci.c`).
- **Микрокода и zap** в комплекте.

### Права службы (`system.conf`)

- `devicetree` на `"qcom,adreno"`, `"qcom,gpucc-sdm660"`,
  `"qcom,gcc-sdm660"`, `"qcom,adreno-smmu"`. Именно `adreno-smmu`, а не
  `smmu-v2`: тот совпал бы с anoc2, lpass и mmss SMMU. Предел
  `RSS_NR_DEVICETREE` — 12; в PLAN.md и в комментарии `system.conf`
  написано «8», это устарело.
- Регион zap (`shared-dma-pool`) строкой `devicetree` давать нельзя:
  выдались бы все пулы. Нужна выдача по phandle `memory-region` или
  ядром внутри вызова SCM (§5).
- Системные права: `IRQCTL`, `CACHECTL`, `UMAP`, `VIRCOPY`, `vm REMAP` —
  как у `mali`.

## 10. Что QEMU не проверяет

У QEMU нет ни Adreno, ни SMMU, ни TZ. На эмуляторе проверяется только
логика.

- Поиск узла: QEMU — «узла нет», служба уходит. Стенд — настоящий DTB
  clover.
- Порядок «сначала такт, потом чтение» — **только моделью на хосте**,
  где чтение блока без такта считается зависанием шины и роняет стенд.
- Интерфейс msm DRM и Mesa целиком — в «виртуальном» режиме службы.
  GPU нет, задания разбираются проверяльщиком PM4 и сразу завершаются
  (§11, шаг 8).

Только на планшете:

- такты, GDSC, напряжение, частоты;
- SMMU: таблицы, кэш их обхода, TLB, атрибуты памяти — то, что QEMU не
  проверяет и на CB2 (CLAUDE.md, «всё, что касается кэшей»);
- TZ и SCM, zap;
- микрокод и CP;
- прерывания GPU и SMMU;
- когерентность буферов с GPU.

## 11. План по шагам

Каждый шаг начинается, только если прошёл предыдущий. «Строки» — что
должно быть на экране или в `kmsg`.

| # | Что | Чем проверяется | Строки, которых ждать |
|---|---|---|---|
| 0 | Этот разбор и план | согласование | — |
| 1 | **Таблицы GPUCC и проба без записей.** `gen-sdm660-tables.py` → `sdm660_gpucc.h` (ветви, корни, PLL, GDSC, BCR). Служба `adreno` (`minix/drivers/video/adreno`): поиск узла GPU, SMMU, GPUCC и zap в дереве; разбор `clocks` по phandle; карта блоков; `stage=probe` — чтение в порядке §2, печать Q1–Q3 (§12); `scheduler KERNEL; cpu 1;` в `system.conf` | хост: стенд `port/test/adreno` на DTB clover, CB2 и QEMU, модель «чтение без такта = зависание», мутации. QEMU: служба уходит со словами; `cpu` соблюдается. Планшет — круг §12, шаг a | `adreno(info)node gpu@5000000 …`, `Q1 gcc: cfg_ahb on …`, `Q2 gpucc: cx off …` |
| 2 | **Питание** (`stage=cx`, `gx`, `gpu`, `smmu` — каждый следующий включает предыдущие; пишет только GCC и GPUCC плюс `CP_SCRATCH_REG0`). Включить GCC-ветви, CX, GX по порядку Linux, gfx3d и rbbmtimer от XO 19,2 МГц. Прочитать GPU и SMMU, выключить всё обратно | хост: модель проверяет порядок записей (BCR, clamp, collapse, ожидания). Планшет — круг §12, шаги b–e | `Q4 power: cx on …, gx on …`, `Q5 gpu: hw …`, `Q7 timer: 192000±…` |
| 3 | **SCM в ядре**: узкий вызов ядра (белый список FID из `system.conf`, адреса — виртуальные, проверка ядром). Сначала только IS_CALL_AVAIL и PAS_IS_SUPPORTED(13) | хост: стенд проверки аргументов. QEMU: `ENODEV` (нет `qcom,scm`). Планшет — круг §12, шаг f, **если согласовано** | `Q12 tz: smccc arm64, pas 13 supported` |
| 4 | **Микрокод и zap.** Скрипт скачивания `a530_pm4/pfp` в `~/gpu-fw` (sha256 закреплён), файл лицензии в образ. `a512_zap.*` — с планшета, `adb pull` под LineageOS. Разбор MDT (хост, на настоящем файле), PAS_INIT/MEM_SETUP/AUTH в регион `0xf0b00000` | хост: разбор MDT против `readelf`. Планшет | `zap: a512_zap.mdt 3 segments, relocatable …; pas 13 authenticated` |
| 5 | **CP.** Обхода SMMU нет (§13: USFCFG 1, SID 0 без SMR), поэтому сначала шаг 6, хотя бы в тождественном отображении. `hw_init` A512, кольцо, ME_INIT, простой. `CP_SET_SECURE_MODE` 0. Первая команда: PKT4 в `CP_SCRATCH`, `CACHE_FLUSH_TS` с IRQ — линия 332 | хост: сборщик PM4 против декодера Mesa (`cffdump`, rnn-XML MIT) — независимый эталон; стенд порядка `hw_init` против списка §6. Планшет | `cp: me_init done, idle in … us`, `irq 332: cache flush ts seqno 1 — the line works` |
| 6 | **SMMU**: CB0, таблицы LPAE в памяти службы, TLB, отказ нарочно (`faulttest=1`, как у `mali`), линия CB0 361 | хост: независимый обходчик таблиц, мутации; модель TLB и некогерентного обхода. Планшет | `smmu: cb0 ias 36 oas 36 …`, `fault at 0x… (fsr 0x…), terminated; next job clean` |
| 7 | **msm DRM** в службе: `libdrmdriver` из `mali_drm.c`, вызовы §8, задания в кольцо, заборы, `WAIT_FENCE`, очереди; переключение TTBR0 между клиентами (§4) | хост: `msmtest` на модели, стенд `mali` до и после выноса. Планшет: `fdtest` — свой зонд вызовов | `/dev/dri/renderD128 is served (msm 1.x)` |
| 8 | **Mesa freedreno** в дереве и **виртуальный режим** службы (`virtual=1`): интерфейс без GPU, PM4 проверяется разбором, заборы сразу | хост: `mesa-import.sh` с `panfrost,freedreno`, `glcube` линкуется, CB2-сборка не изменилась; `FD_RD_DUMP` с QEMU → хостовый `cffdump` без ошибок разбора. QEMU: `glcube` проходит все кадры (картинки нет) | QEMU: `GL_RENDERER: FD512` (виртуальный); `glcube` на CB2 — по-прежнему `Mali-G52` |
| 9 | **glcube на планшете** | планшет по ssh: вывод `glcube`, кадр — `board-shot.sh`-подобным снимком `/dev/fb0` в PNG на хост | `GL_RENDERER: FD512`, `15 of 15`, кадры/с |
| 10 | **Частоты**: голос VDDMX через `qcom_rpm` (новый запрос службы), PLL «пинг-понг», до бинового предела; гашение GDSC по простою | планшет | `gfx3d 588 MHz (pll1), mx nom` |

**До планшета делаются:**

- шаги 1 и 2 — код, стенд, QEMU;
- шаг 3, если согласовано;
- хостовая часть шага 5: сборщик PM4 против `cffdump`;
- шаг 8 в части Mesa — сборка.

Потом — круг §12 на планшете.

## 12. Первый круг на планшете: по ssh, без человека

Планшет на кабеле у хаба, человека рядом нет (2026-10-17): фото и кнопок
нет. Канал есть с шага 5 `hw-net.md`:

- `ssh clover-usb` — корень на `userdata`, `sshd`;
- круг — `port/clover/cycle.sh`;
- `hub-keeper.sh` на хабе ставит образ, как только планшет показался в
  fastboot.

Служба — программа корня. Её двоичный файл и `system.conf.d/adreno`
кладутся на eMMC через `scp`, образ загрузки для этого не пересобирается.
Новый образ нужен только с правкой ядра — шаг f.

### Как не оставить планшет без рук

- **После сброса** (bootwd, PSCI) планшет уходит в fastboot, только если
  `qcom_pmic` этого образа поставил `nextboot=bootloader`.
  - У командной строки `cycle.sh` по умолчанию это есть, вместе с
    `bootwd=1800`. Перед перезагрузкой `cycle.sh` ещё и делает
    `sysctl -w minix.pmic.nextboot=bootloader`.
  - **Что прошито в `boot`, неизвестно** (clover-net, 2026-10-17): все её
    образы шли через `fastboot boot`. Образы комплекта `FLASH.md` сделаны
    без `bootwd` и без `nextboot`, откат — `boot.img` LineageOS.
  - Отсюда правило: **у каждого образа круга — `nextboot=bootloader`**.
    Служба `adreno` стартует много позже `qcom_pmic`, так что к её шагам
    регистр уже стоит. Сброс раньше `qcom_pmic` увёл бы в неизвестный
    `boot`; к GPU это не относится.
  - Хранитель должен работать: `cycle.sh` запускает его сам. 2026-10-17 он
    запущен в 13:01 на 12 часов, планшет на парковке.
- **Хуже всего** — зависание шины, которое останавливает и загрузочное
  ядро. Тогда нет тика и нет bootwd, и вернуть планшет могут только
  кнопки. Сторожа SoC у MEECHO на clover нет: в дереве mainline нет даже
  узла `watchdog@17817000`, хотя у LineageOS он есть (`qcom,wdt`).

Защита от этого — в трёх слоях:

1. **Порядок «такт → чтение»** (§2). Его проверяет модель на хосте, где
   чтение без такта — провал стенда.
2. **Служба не на загрузочном ядре.** `sched` любой системный процесс
   ставит на BSP (`pick_cpu()`, `is_system_proc()` в
   `servers/sched/schedule.c:162`). Поэтому в `system.conf` —
   `scheduler KERNEL; cpu 1;`: тогда ядро ставит процесс на заданное ядро
   (`lib/libsys/sched_start.c:69-72`). Если повиснет ядро службы, тик
   загрузочного идёт дальше, и bootwd сработает. Что это соблюдается на
   aarch64 — проверить на QEMU (`QEMU_SMP=4`) [I].
3. **Риск дробится.** Каждый шаг — отдельный запуск службы по ssh;
   следующий — только после ответа предыдущего. Если планшет перестал
   отвечать, известно, на каком шаге.

Остаётся одно: зависание всей шины конфигурации вместе с путём сброса
через TZ. Только тогда нужен человек.

### Шаги круга

Круг идёт двумя скриптами из Git Bash:

1. `port/clover/cycle.sh` с `KERNEL_ELF=` ядра с `sys_qcomscm` поднимает
   планшет с корнем eMMC.
2. `port/clover/gpu-probe.sh` кладёт службу, её `adreno.conf` и
   `minix-service`, знающий `QCOMSCM`, в `/root/adreno`. Системные файлы
   он не заменяет. Затем гоняет шаги по одному: между шагами — `true` по
   ssh, после каждого — новые строки журнала ядра. Журнал —
   `D:\minix\clover-flash\gpu\probe-*.txt`.

| Шаг | Аргументы | Что пишет | Вопросы |
|---|---|---|---|
| a | `stage=probe tz=1` | ничего; SMC IS_CALL_AVAIL и PAS_IS_SUPPORTED(13) | Q1–Q3, Q12 |
| b | `stage=cx` | CX GDSC — и обратно | Q4 (CX), Q11 |
| c | `stage=gx` | + GX GDSC; ветви GCC `mem`, `mem_iface`; gfx3d, rbbmtimer, rbcpr от XO — и обратно | Q4, Q11 |
| d | `stage=gpu` | + чтение GPU и одна запись в `CP_SCRATCH_REG0` | Q5–Q8 |
| e | `stage=smmu` | + чтение SMMU, без записей | Q9–Q10 |

TrustZone спрашивается в шаге a, до поиска узла GPU. QEMU отвечает
`ENODEV`.

Строки службы — `adreno(info)Qn …`, каждая не длиннее ~75 знаков. Столько
входит в ширину экрана (1200 точек, шрифт 8×16 ×2), так что те же строки
читаются и с экрана, если рядом окажется человек.

| Q | Вопрос | Что решает |
|---|---|---|
| Q1 | GCC: включены ли CFG_AHB, BIMC_GFX, GPU_BIMC_GFX, голоса GPLL0 | можно ли читать GPUCC; что оставил ABL |
| Q2 | GPUCC: cxo, CX, GX, gfx3d (CBCR и HW_CTL), rbbmtimer, rbcpr | состояние после ABL |
| Q3 | PLL0/PLL1: MODE, L, ALPHA, частота; gfx3d — источник и делитель | что уже настроено |
| Q4 | Включаются ли CX и GX и за сколько микросекунд | **есть ли рельс GX** — проверка вывода «GX от MX» (§3) |
| Q5 | GPU 0x0000–0x0002 и `VBIF_VERSION` | номер железа; GPU отвечает |
| Q6 | `RBBM_STATUS`, `RBBM_INT_0_STATUS`, `SECVID_TRUST_CNTL`, `CP_PFP_ME_CNTL` | состояние после сброса; secure mode |
| Q7 | ALWAYSON_COUNTER за 10 мс | таймер GPU идёт от 19,2 МГц |
| Q8 | запись и чтение `CP_SCRATCH_REG0` | запись в GPU доходит |
| Q9 | SMMU ID0/ID1/ID2, sCR0 | число CB и SMR, ias/oas, размер страницы; кто держит SMMU |
| Q10 | SMR/S2CR для SID 0, CB0 SCTLR/TTBR0/TCR | обход или трансляция, оставленная загрузчиком; можно ли S2CR без TZ |
| Q11 | выключение: GX off, CX off, ветви как были | домены гаснут; ничего не оставлено включённым |
| Q12 | IS_CALL_AVAIL и PAS_IS_SUPPORTED(13) | работает ли путь zap |

`adreno(warn)…refusing` значит, что служба отказалась, ничего не тронув.
Причина — в той же строке.

**Zap с того же кабеля.** Файлы `a512_zap.*` (шаг 4) лежат в разделе
vendor (ext4), а у MEECHO нет ext4. Есть два пути:

- **[?] fastboot boot recovery LineageOS с adb.** Сначала проверить, что
  этот ABL его примет и что recovery даёт `adb pull` раздела `vendor`.
- **Свой разбор образа ext4 на хабе.** Раздел `vendor` читается сырым из
  самой MEECHO и разбирается `debugfs` на хабе. Но с `gptpart0=userdata`
  драйвер вне окна не пускает и на чтение, поэтому нужен образ без окна:
  корень в памяти, сеть и `sshd` на ramdisk. Сейчас ramdisk без `sshd`
  (`hw-net.md`, код 104) [?].

Ничего из этого не пишет на планшет.

## 13. Первый круг на планшете [T] (2026-10-17)

Ядро `obj-kvar/gpu` (с `sys_qcomscm`), `boot.mba` шага 4б сети, корень на
eMMC. Образ поставил хранитель через `cycle.sh`; ssh был через 22 с.
Шаги прогнал `gpu-probe.sh`. Журнал — `D:\minix\clover-flash\gpu\probe-1.txt`.
После каждого шага планшет отвечал, Q11 каждый раз — «all as found».
Служба на планшете — на CPU 1 (`ps -o cpuid`, `hold=6`).

| Q | Ответ | Что из этого следует |
|---|---|---|
| Q1 | `iface` on (`20008001`), `mem` (BIMC_GFX) **off**, `mem_iface` (GPU_BIMC_GFX) **on** (`1`), голоса GPLL0 off | ABL держит шину конфигурации GPU и половину памяти |
| Q2 | CX off, GX off, cxo on, rbcpr/gfx3d/rbbmtimer off | GPU после ABL выключен целиком |
| Q3 | PLL0: L `0x3d` — 1176 МГц (= 2·588), без захвата, выход off. PLL1: L `0x21` + alpha — 639,9 МГц, off. Все три корня — на кварце, корни off | PLL настроены, но не включены |
| Q4 | CX on за 12–13 мкс, **GX on за 12 мкс** | **рельс GX есть** при том MX, что держит загрузчик; вывод «GX от MX» (§3) не опровергнут |
| Q5 | 0x0000–0x0002: `000000a2 d510000b 02372482`; `VBIF_VERSION` `20110000` | GPU отвечает; что значат три слова — [?] |
| Q6 | `RBBM_STATUS` `00000001` (только HI_BUSY), INT `0`, **`SECVID_TRUST_CNTL` `1`**, `CP_PFP_ME_CNTL` `00000303` | **GPU в secure mode**: без zap (или прямой записи 0) CP не пойдёт в обычную память |
| Q7 | 192083–192085 тактов за 10 мс | таймер GPU — 19,2 МГц, путь `rbbmtimer` верен |
| Q8 | `CP_SCRATCH_REG0` 0 → записано `5ad0c0de`, прочитано то же, возвращено 0 | запись в GPU доходит |
| Q9 | sCR0 `00200626`: CLIENTPD 0, **USFCFG 1**, GFRE/GFIE/GCFGFIE. ID0 `48016603`, ID1 `20008005`, ID2 `00007511`: 5 банков, **3 SMR**, страницы 4K, ias/oas 36, ubs 48 | SMMU включён, и **поток без SMR — отказ, а не обход** |
| Q10 | **ни один SMR не ведёт SID 0**; CB0 (`+0x8000`) весь в нулях | GPU сейчас не может ни читать, ни писать память: без SMR/S2CR для SID 0 каждый его доступ — отказ |
| Q12 | SMCCC64; IS_CALL_AVAIL — 1 у PAS_INIT_IMAGE, MEM_SETUP, AUTH_AND_RESET, PAS_IS_SUPPORTED, SET_REMOTE_STATE; **PAS_IS_SUPPORTED(13) = 0** | путь zap у TZ есть. «Не поддерживается» ничего не решает: Linux перед zap этого не спрашивает (`adreno_gpu.c:30-184`), ответ даст только загрузка (шаг 4) |

**Что это меняет в плане (§11).**

- **SMMU раньше CP.** Обхода нет: USFCFG = 1, поток SID 0 не
  маршрутизирован. Первое, что нужно GPU для любого доступа к памяти, —
  SMR → S2CR → CB0. Шаг 5 начинается с SMMU (бывший шаг 6), и сначала
  проверяется, что NS-сторона вообще может записать SMR и S2CR: запись,
  чтение обратно, восстановление. Linux это делает из NS (`arm-smmu.c`),
  но на этом TZ — не проверено.
- **Zap нужен.** `SECVID_TRUST_CNTL` = 1. Ни одного файла `a512_zap.*` пока
  нет (§5).
- **Регистр 0x0000** (`000000a2`) похож на номер железа [I] — сверить
  с чем-нибудь независимым, когда будет с чем.


## 14. SMMU раньше CP: стадии `smr`, `cb0`, `fault` [C]

По §13 первым делом GPU нужен маршрут SID 0 → S2CR → банк 0. Это шаг 6
плана (§11), поставленный перед шагом 5. Сделан в службе (`adreno_smmu.c`,
таблицы — `adreno_pt.c`), проверен стендом; **на планшете ещё не
запускался**.

Ни одна стадия не трогает sCR0, чужие SMR, банки 1–4 и глобальные TLBI.
Всё записанное возвращается и перечитывается (Q17).

| Шаг круга | Аргументы | Что пишет | Вопросы |
|---|---|---|---|
| f | `stage=smr` | свободный SMR (без VALID) и его S2CR: запись, чтение, назад | Q13 |
| g | `stage=cb0` | + банк 0 над таблицами в памяти службы; трансляции самим SMMU; SID 0 в банк 0 на 10 мс | Q14, Q15 |
| h | `stage=fault` | + отказ в банке через FSRRESTORE, линия 361 | Q16 |

```
gpu-probe.sh smmu smr cb0 fault
```

| Q | Вопрос | Что решает |
|---|---|---|
| Q13 | держатся ли записи в SMR/S2CR с этой стороны; сколько разрядов ID и MASK | можем ли мы маршрутизировать поток GPU сами, или это делает только TZ |
| Q14 | что SMMU сам (ATS1*) говорит о таблицах: 7 трансляций — RW, RO, не отображённое, после снятия и TLBIASID | формат таблиц, регистры банка, атрибуты (PAR.ATTR 0x44), очистка кэша, TLBI |
| Q15 | SMR с VALID для SID 0 → банк 0: читается ли обратно; FSR и sGFSR за 10 мс | маршрут работает; не обращается ли GPU к памяти сам |
| Q16 | линия 361: тишина до, одно прерывание на FSRRESTORE TF, тишина после очистки | линия банка 0 и обработчик отказа |
| Q17 | всё ли вернулось | ничего не оставлено |

Строки, которых ждать (стенд, модель с ID-регистрами планшета):

```
Q13 smr 1 (…): id bits 7 mask bits 7 (numsidb 3)
Q13 s2cr wrote … read …; back … …: the table takes writes
Q14 cb0: tables at …, ias 48 oas 36, 4 levels; sctlr 00001167 tcr 00802010 tcr2 00038011
Q14 cb0: 7 of 7 translations as the tables say; fsr after them 0
Q15 sid 0 by smr 1 to cb0: smr 80000000 s2cr 00000000; 10 ms: fsr 0 gfsr 0; taken back
Q16 line 361 quiet before; raised tf: it came, fsr 00000002 …
Q16 cleared (fsr 00000000), quiet after: the line works
Q17 smmu put back: smr 1 cb0; all as found
```

Решения:

- **ias 48, а не 36.** Для stage 1 Linux берёт вход по ID2.UBS (48) и выход
  по ID2.IAS (36); в таблице §11 у шага 6 было «ias 36» — неверно.
- **Трансляции, а не GPU.** ID0.ATOSNS = 0 на планшете (`0x48016603`, бит
  26): у SMMU есть ATS1PR/PW/UR/UW и PAR. До CP это единственный способ
  заставить SMMU обойти наши таблицы. Что проверяется: формат, банк,
  атрибуты, очистка, TLBI. Что нет: что GPU выдаёт SID 0 и что его
  обращения идут через банк 0 — это шаг 5.
- **ATS1* пишутся одним 64-битным обращением** (`ad_wr64`), как у Linux:
  операция начинается по записи.
- **Отказ через FSRRESTORE** (0x5c, архитектура SMMUv2): так линию банка
  поднимает самотест прерываний Qualcomm (downstream `arm-smmu.c`, факт, не
  код). Отказ не настоящий — линия и обработчик настоящие.
- **Попадает ли отказ трансляции в FSR, решает реализация.** Служба
  печатает FSR после трансляций (`fsr after them`) и очищает его до стадии
  `fault`, чтобы прерывание не пришло «заранее».
- Права службы теперь: `QCOMSCM`, `IRQCTL`, `UMAP`, `CACHECTL`.

Стенд: `port/test/adreno`, модель SMMU — `smmumodel.c` (независимый
обходчик, TLB с устаревшей записью, кэш процессора и ОЗУ раздельно,
мусор 0x5b в новых страницах). 298 проверок, мутаций 32 из 32.

**Микрокод** (шаг 4, первая половина): `port/clover/fetch-gpu-fw.sh` →
`~/gpu-fw/qcom/a530_{pm4,pfp}.fw` + `LICENSE.qcom`, `NOTICE.qcom`;
linux-firmware `12dfe670`, sha256 закреплены. PFP: слова 1 и 3 —
`0x005ff08a`, `0x00087001` → `has_whereami`.

**Zap** (шаг 4, вторая половина) — способ получения ждёт решения
пользователя, все три не пишут на планшет:

- (а) второе окно драйвера eMMC **только на чтение** (`vendor`), `dd`
  раздела по ssh, разбор ext4 на хосте — образ MEECHO, чужой системы нет;
- (б) `fastboot boot` recovery LineageOS и `adb pull` — без человека adb в
  recovery может не включиться;
- (в) `vendor.img` из официальной прошивки Xiaomi для clover — планшет не
  нужен, но файл не с этого устройства (подпись проверит TZ).

PAS_INIT_IMAGE / MEM_SETUP / AUTH_AND_RESET в `sys_qcomscm(2)` не начаты.
Замысел: регион `gpu@f0b00000` выдаёт службе RS — по `memory-region`
узла `zap-shader` у узла GPU, который службе уже выдан. Ядро в MEM_SETUP
переводит виртуальный адрес вызывающего и принимает только диапазон
целиком внутри no-map области дерева.
