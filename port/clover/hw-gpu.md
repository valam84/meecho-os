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
- **Шаги 1–6 пройдены на планшете (2026-10-17, §13–§17).** TrustZone
  загрузил и принял zap-шейдер планшета, CP запущен, GPU вышел из secure
  mode (`SECVID_TRUST_CNTL` 1 → 0) и выполнил первую команду: забор в
  памяти и прерывание на линии 332.
- **Шаг 7 — msm DRM 1.6 в службе — пройден на планшете (§18)**: узел
  `/dev/dri/renderD128`, буферы со своим пространством у каждого
  клиента, задания в форме `a5xx_submit()`, `WAIT_FENCE` по линии 332,
  отказ SMMU — отказом задания. Зонд `fdtest`: задание пишет в буфер,
  забор дождан, два клиента на одном адресе видят своё, 100 заданий
  подряд — 0 отказов. Первый прогон нашёл одну ошибку: чтение
  `CP_RB_RPTR` само занимает CP.
- **Куб на экране планшета (§19)**: `glcube` через Mesa freedreno —
  `FD512`, OpenGL ES 3.1, 15 проверок из 15, 2,5 кадра/с на кварце.
  Пока с двумя обходами в окружении: кэш шейдеров (нет build-id) и
  компиляция в главном потоке (стек `pthread` 8 КиБ).

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

**Где лежат файлы** (и микрокод, и zap; ни один не в дереве):

| Где | Что |
|---|---|
| `~/gpu-fw/qcom` (WSL) | `a530_pm4.fw`, `a530_pfp.fw` — `port/clover/fetch-gpu-fw.sh`, linux-firmware `12dfe670`, sha256 закреплены; рядом `LICENSE.qcom`, `NOTICE.qcom` |
| `D:\minix\clover-flash\gpu\zap` | `a512_zap.mdt`, `.b00`–`.b02`, `.elf` — сняты с раздела `vendor` этого планшета (§15) |
| `D:\minix\clover-flash\gpu\fw\qcom` | комплект для планшета: оба набора и тексты лицензии |
| `/libdata/firmware/qcom` на планшете | туда их кладёт `gpu-probe.sh` (`DIR/fw/qcom/*`); там их читает служба (`adreno_fw.c`, `fwdir=`) — каталог, где их искал бы `firmload(9)` NetBSD и где Linux держит `/lib/firmware/qcom` |

### Zap-шейдер и TrustZone

**Зачем.** GPU выходит из сброса в secure mode. Выход — `CP_SET_SECURE_MODE`
(0x66) с аргументом 0 из кольца, и CP исполняет его, только если TZ
загрузил и проверил zap. Запасной путь Linux — запись
`RBBM_SECVID_TRUST_CNTL` (dword 0xf400) = 0 с процессора, но только при
`-ENODEV` (узла `zap-shader` нет). Комментарий Linux: если TZ закрыл
регистр, «a permissions violation will soon follow»
(`a5xx_gpu.c:976-1003`). На clover узел есть, значит Linux идёт через zap.
[I] Для MEECHO zap обязателен, пока проба не покажет обратного.

**Разбор MDT** (`mdt_loader.c`; у MEECHO — `adreno_mdt.c`, чистый C,
его же собирает стенд):

- файл — ELF32 (Linux другой не читает); заголовки программ по 32 байта и
  внутри файла; секции, если есть, — тоже;
- ph0 не загружаемый: его `p_filesz` — размер заголовков, с них
  начинаются метаданные;
- hash-сегмент — первый после ph0 с типом сегмента 2 (`p_flags` 26:24);
  метаданные продолжаются им **без зазора**: сразу за заголовками, если
  файл ровно «заголовки + хэш» (так у планшета, 0x94 + 0x1988 = 6684);
  по его `p_offset`, если тот внутри файла; иначе из своего `.bNN`;
- загружаемые сегменты — PT_LOAD, не хэш, `memsz` ≠ 0; образ — от
  наименьшего `p_paddr` до наибольшего конца, **округлённого вверх до
  4 КиБ**; перемещаемый, если хоть у одного стоит бит 27;
- образ «расщеплён», если хоть один сегмент с байтами кончается за
  `.mdt`: тогда **каждый** загружаемый сегмент читается из своего файла —
  имя `.mdt` с последними тремя буквами `bNN`, NN — номер заголовка, и
  размер файла обязан равняться `p_filesz`.

**Порядок вызовов** (Linux: `zap_shader_load_mdt()`, `qcom_mdt_load()`;
у MEECHO — `adreno_zap.c`):

1. `.mdt` прочитан и разобран, **каждый** нужный `.bNN` прочитан — до
   первого вызова TZ. Недостающий файл останавливает здесь, ничего не
   отдав TrustZone.
2. Регион `zap-shader` (`memory-region`, `gpu@f0b00000`, 8 КиБ,
   `no-map`) отображён и каждая страница тронута: ядро ищет память
   вызывающего по его таблице, а отображение физической памяти
   заполняется по первому касанию.
3. **PAS_INIT_IMAGE**(13, метаданные): заголовки + хэш в физически
   непрерывном буфере с начала страницы (у Linux — некэшируемом; у MEECHO
   ядро чистит его до памяти). Буфер освобождается после ответа.
4. **PAS_MEM_SETUP**(13, начало региона, `max − min`) — только у
   перемещаемого образа. У планшета: `(13, 0xf0b00000, 0x2000)` — конец
   0x5000 + 0x1334 округлён до 0x7000. С этого вызова регион — TrustZone.
5. Сегменты копируются со смещением `p_paddr − min` (у неперемещаемого —
   `p_paddr − начало региона`), остаток до `memsz` — нулями. MEECHO пишет
   их словами (регион отображён как Device: невыровненный доступ — отказ),
   концы — байтами, и читает всё обратно.
6. Отображение снято, **PAS_AUTH_AND_RESET**(13). У Linux — при
   работающем GPU, из `hw_init()` после `CP_ME_INIT`; у MEECHO в стадии
   `cp` — там же (§17).
7. В кольцо — `CP_SET_SECURE_MODE` 0.
8. Каждый следующий подъём GPU образ не перегружает: только
   **SET_REMOTE_STATE**(0, 13) — «resume» (`a5xx_zap_shader_resume()`),
   регион больше не трогается.

Успех PAS-вызова — `x0 == 0` **и** `x1 == 0`: в x1 TrustZone отвечает
собственным приговором образу (Linux: `ret ? : res.result[0]`).

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

- [T] На планшете он в `/vendor/firmware/a512_zap.mdt` и `.b00..b02` —
  снят оттуда окном `sdmmc` только на чтение (§15).
- **В репозиторий не класть**, в опубликованные образы — тоже. Только
  локальный комплект в `D:\minix\clover-flash` (таблица выше).
- Перемещаемый, 0x1334 байта кода, образ 0x2000 — ровно регион (§15).

**MEECHO [C]: вызов ядра `sys_qcomscm(2)`** (`<minix/qcomscm.h>`,
`kernel/arch/aarch64/qcom_scm.c`). У процесса на EL0 SMC нет, а сквозной
«SMC с любыми аргументами» не годится: PAS_INIT_IMAGE и MEM_SETUP получают
**физические адреса**, и TZ запирает ими память. Поэтому ядро предлагает
операции, а не инструкцию:

| Операция | Вызов TZ | Что проверяет ядро |
|---|---|---|
| `QCOMSCM_AVAIL`, `PAS_SUPPORTED` | IS_CALL_AVAIL, PAS_IS_SUPPORTED | — (вопросы) |
| `QCOMSCM_PAS_STATE` | — | ничего не спрашивает у TZ: что это ядро сделало с периферией с загрузки — `NONE`, `INITED`, `GIVEN` (память названа TZ), `AUTHED` |
| `QCOMSCM_PAS_INIT` | PAS_INIT_IMAGE(id, pa) | буфер — адрес вызывающего с начала страницы, одна физическая полоса, обычная записываемая RAM линейного отображения; ядро чистит его до памяти (`CACHE_CLEAN_INVALIDATE`); у `AUTHED` — `EALREADY` |
| `QCOMSCM_PAS_MEM_SETUP` | PAS_MEM_SETUP(id, pa, len) | только после INIT; диапазон — отображение вызывающего, одна полоса, **вне** линейного отображения и **целиком внутри** `no-map`-потомка `/reserved-memory`; до вызова — `GIVEN` |
| `QCOMSCM_PAS_AUTH` | PAS_AUTH_AND_RESET(id) | только после INIT; до вызова — `GIVEN`, при успехе — `AUTHED` |
| `QCOMSCM_REMOTE_STATE` | SET_REMOTE_STATE(0, id) | только «resume» и только для периферии, которую это ядро загрузило |

- Перед каждым SMC — `dsb sy`: образ лёг в регион через отображение
  Device с этого же процессора.
- **Прерванный вызов.** Yielding-вызов TZ останавливает ради прерывания
  нормального мира (x0 = 1). Linux берёт прерывание и продолжает; ядро
  MEECHO держит прерывания выключенными весь вызов ядра, и продолжение в
  ядре снова наткнулось бы на то же прерывание. Поэтому вызов
  **держится** (один, вызывающего, на том процессоре, где встал),
  вызывающий получает `QCOMSCM_INTERRUPTED`, прерывание берётся в
  пользовательском режиме, и `QCOMSCM_RESUME` продолжает: x0 = 1, те же
  x1..x5 и x6, отданный прошивкой. Цикл — в `sys_qcomscm()` libsys,
  вызывающий его не видит; пока вызов держится, прочие операции —
  `EBUSY`. Служба стоит на CPU 1 (`cpu 1;`), так что «тот же процессор»
  выполняется сам.
- **Регион выдаёт RS.** Строка `devicetree` теперь выдаёт и диапазоны
  `/reserved-memory`, которые совпавший узел или его потомок называет в
  `memory-region` (`servers/rs/manager.c`): у узла GPU это потомок
  `zap-shader`, 8 КиБ на 0xf0b00000. По phandle — только этот диапазон;
  совместимость `shared-dma-pool` выдала бы все пулы.
- Права службы: `system QCOMSCM;` в `adreno.conf`.

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

- ~~**Кода ARM SMMU**~~ — есть у службы `adreno` (`adreno_smmu.c`,
  `adreno_pt.c`, §14–§16); общего драйвера IOMMU по-прежнему нет.
- ~~**SMC для служб**~~ — `sys_qcomscm(2)`, узкими операциями (§5).
- ~~**Микрокода и zap** в комплекте~~ — в комплекте, вне дерева (§5,
  «Где лежат файлы»).

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

**Состояние (2026-10-17, вечер):** шаги 1–6 пройдены на планшете — 1–3
(§13), 6 переставлен перед 5 (§14–§16), 4 и 5 (§17). Шаг 7 пройден на
планшете (§18). Шаг 8 — частью: freedreno в дереве (`6f6631ad8`, один
`libgallium` с panfrost), `glcube` с ним линкуется; виртуального режима
службы и проверки `FD_RD_DUMP` через `cffdump` нет, `glcube` на CB2 с
этим `libgallium` (и `mali` на `libdrmdriver`) не проверялся. Шаг 9
пройден раньше 8 — куб на экране планшета (§19), пока с двумя обходами
в окружении. Дальше: обходы — правками системы; CB2 — тем же `glcube`;
шаг 10 (частоты).

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

**Zap** (шаг 4, вторая половина) — способ получения ждал решения
пользователя (выбран (а), §15), все три не пишут на планшет:

- (а) второе окно драйвера eMMC **только на чтение** (`vendor`), `dd`
  раздела по ssh, разбор ext4 на хосте — образ MEECHO, чужой системы нет;
- (б) `fastboot boot` recovery LineageOS и `adb pull` — без человека adb в
  recovery может не включиться;
- (в) `vendor.img` из официальной прошивки Xiaomi для clover — планшет не
  нужен, но файл не с этого устройства (подпись проверит TZ).

~~PAS_INIT_IMAGE / MEM_SETUP / AUTH_AND_RESET в `sys_qcomscm(2)` не
начаты.~~ Сделаны так, как здесь был замысел: регион выдаёт RS по
`memory-region` потомка `zap-shader`, ядро в MEM_SETUP принимает только
диапазон целиком внутри `no-map` области (§5, §17).

## 15. Второй круг: окно `vendor`, zap снят; SMMU и гипервизор [T] (2026-10-17)

Ядро `obj-kvar/gpu`, `boot.mba` шага 4б сети с одним заменённым файлом
(`/service/sdmmc`, `port/clover/mba-swap.py`: остальные 67 файлов ramdisk
сверены — те же), `gptro0=vendor` в командной строке. Журналы —
`D:\minix\clover-flash\gpu\probe-2.txt`, `probe-3.txt`.

**Окно только на чтение работает.** `sdmmc`: `vendor: 4194304 sectors from
6684672, read only, as minor 4`, `vendor starts with ext4`; `/dev/c0d0p3`.

**Zap снят, ничего не записано.** Полная копия раздела по ssh шла бы часы:
канал хаба отдаёт ~100–180 КиБ/с (на самом планшете `dd` раздела — 33
МБ/с). `port/clover/ext4-pull.py` читает только нужные блоки ext4: 12
запросов, 19 блоков, 27 с. Из того же кэша (разреженный образ) `debugfs
dump` даёт те же байты — проверка, не имеющая общего кода с разборщиком.

| Файл (`/vendor/firmware/`) | Байт | md5 |
|---|---|---|
| `a512_zap.mdt` | 6684 | `ba043f7f…` |
| `a512_zap.b00` | 148 | `0c29920a…` |
| `a512_zap.b01` | 6536 | `2abfdc85…` |
| `a512_zap.b02` | 4916 | `3ea17369…` |
| `a512_zap.elf` | 17204 | `afa55f7a…` (тот же образ одним файлом) |

Файлы лежат только в `D:\minix\clover-flash\gpu\zap` (вне дерева); копия
для планшета — в комплекте `fw\qcom`, оттуда `gpu-probe.sh` кладёт её в
`/libdata/firmware/qcom` (§5, «Где лежат файлы»).

**MDT (сверено с `readelf` и с `.elf`):** ELF32, `QUALCOMM DSP6`, вход
0x5000, три заголовка программы:

| | Тип | Смещение | paddr | filesz / memsz | Флаги | Что |
|---|---|---|---|---|---|---|
| ph0 | NULL | 0 | 0 | 0x94 / 0 | `0x07000000` (сегмент типа 7) | заголовки ELF и программ |
| ph1 | NULL | 0x1000 | 0x7000 | 0x1988 / 0x2000 | `0x02200000` (тип 2 — HASH) | подпись и хэши |
| ph2 | LOAD | 0x3000 | 0x5000 | 0x1334 / 0x1334 | `0x08000007` — **RELOCATABLE** (бит 27), RWX | код zap |

- `.mdt` = ph0 + ph1 подряд (0x94 + 0x1988 = 6684 байт) — это и есть
  метаданные для PAS_INIT_IMAGE; `.b00`–`.b02` — сегменты по смещениям
  `.elf`.
- Перемещаемый: PAS_MEM_SETUP нужен — `(13, 0xf0b00000, 0x2000)`, а не
  `0x1334`: размер — от наименьшего `p_paddr` (0x5000) до конца,
  округлённого вверх до 4 КиБ (0x6334 → 0x7000; `qcom_mdt_get_size()`,
  `__qcom_mdt_pas_init()`). Сегмент ложится в начало региона
  `gpu@f0b00000` (8 КиБ), образ занимает его ровно; так и прошло (§17).
- Расщеплённый (`.mdt` кончается на 6684 байтах, а ph1 и ph2 лежат дальше):
  метаданные — весь `.mdt`, код — из `.b02` (номер заголовка), `.b00` и
  `.b01` загрузчику не нужны.
- В том же каталоге vendor свои `a530_pfp.fw` (17108) и `a530_pm4.fw`
  (19652) — не те, что в linux-firmware (16144, 19572). План — микрокод
  linux-firmware, как у mainline; вендорский — запасной.

**SMMU: SMR пишутся, S2CR — через гипервизор.**

- Q13 (первая версия проверки): все три SMR свободны, у каждого S2CR
  `0x000200ff`; в SMR держатся все 15 разрядов ID и MASK. Запись S2CR
  «fault, bank 0» (`0x00020000`) прочитана как `0x000200ff` — **не
  принята**.
- Linux знает это поведение (`arm-smmu-qcom.c`, `qcom_smmu_cfg_probe()`):
  «with some firmware versions writes to S2CR of type FAULT are ignored» —
  гипервизор перехватывает регистры сопоставления потоков. Для SMMU
  Adreno Linux этой обработки не включает и пишет S2CR «translate, bank 0»
  — **после** того, как банк 0 настроен (`arm_smmu_attach_dev()`: сначала
  контекст, потом SME).
- Вторая версия проверки записала в ту же неиспользуемую запись
  «translate, bank 0» (`0x00000000`) **до** настройки банка — когда банк
  0 ещё stage-2 (CBAR 0), то есть того вида, каким пользуется сам
  гипервизор. **Планшет встал целиком**: USB-устройство осталось, ping
  нет, ssh нет, строки стадии не дошли.
- **Экран планшета после остановки** (фото человека, 16:44,
  `D:\minix\fotos\IMG_1131.jpg`): последняя строка — `Q10 cb0 +0x8000 …
  fsr 00000000`, конец чтения SMMU; `Q13` нет (она печатается после всех
  записей проверки и их возврата). Паники нет, экран не обновлялся — встала
  вся машина, не одна служба на CPU 1. Выше на том же экране — первая
  версия проверки той же загрузки, прошедшая до `Q13 … writes do NOT
  stick`: те же записи SMR и S2CR «fault» безвредны.
- **Вернул планшет человек**: 16:47 — кнопками в fastboot (сторож этой
  загрузки, `bootwd=2400`, сработал бы около 16:56 — если тик был жив;
  неизвестно). 16:53 хранитель поставил парковку. Такая остановка
  считается требующей человека, пока не доказано обратное.
- [I] Причина — эта запись (единственное отличие от первой проверки, на
  той же загрузке). Механизм не установлен.

Что изменено в службе (`bfae5a483` и следом):

- проверка S2CR пишет только «fault» — значение, которое ничего никуда не
  направит; принята ли запись — только сообщается (`taken` /
  `ignored: a hypervisor's`), решают SMR;
- «translate» пишется только в `route()` — после настройки банка 0, как у
  Linux;
- раз запись «fault» не принимается, S2CR после маршрута вернуть нельзя:
  он останется «translate, bank 0». Тогда `smmu_undo()` выключает банк 0,
  но оставляет его stage-1 (не возвращает CBAR 0) и пишет это в Q17;
- модель стенда считает провалом S2CR «translate» в ненастроенный банк и
  возврат банка в stage-2 под таким S2CR; сценарий `tablet-measured` —
  планшет как измерен. 315 проверок, мутаций 35 из 35.

**Стадии `cb0` и `fault` на планшете ещё не исполнялись.** Следующий круг
с ними — снова запись в регистры, которые перехватывает гипервизор, по
порядку Linux; планшет мог встать от такой записи, поэтому — только с
согласия пользователя.

### Третий круг: банк 0 работает, маршрут останавливает машину [T] (17:05–17:12)

Журналы — `probe-4.txt`, `probe-5.txt`; экран — `D:\minix\fotos\IMG_1132.jpg`.

- **Q13**: SMR пишутся (15 бит ID и MASK), S2CR «fault» игнорируется —
  `(ignored: a hypervisor's)`; всё как было.
- **Q14: 7 из 7 трансляций — как говорят таблицы.** SMMU сам обошёл
  таблицы LPAE в памяти службы: `1000000 → f0e5f000`, `a5a5a5a5a000 →
  f0fc2000` (страницы службы), запись в страницу только для чтения — PF,
  не отображённые адреса и адрес после снятия + TLBIASID — TF. Формат
  таблиц, регистры банка, очистка кэша, TLBI — работают на железе.
- **PAR.ATTR = 0x40**, не 0x44: этот SMMU отдаёт в PAR только внешнюю
  половину байта MAIR. Служба теперь судит по адресу, атрибут печатает.
- **FSR = 0x400** после трансляций — поле FORMAT (биты 10:9 = 2, AArch64),
  не отказ. Сверки «как было» и проверка линии смотрят только на биты
  отказа (`FSR_FAULT`). Первый прогон `cb0` из-за этого сказал «cb0
  differs» — ложно.
- **`route()` останавливает машину.** Сразу после строки `7 of 7`: S2CR
  «translate, bank 0» (теперь — в настроенный банк, порядок Linux), затем
  SMR с VALID для SID 0. Экран застыл, паники нет, вернул человек
  кнопкой. Какая из двух записей — не различить: между ними нет строки.
- Значит, гипервизор этой прошивки не пускает NS-сторону маршрутизировать
  поток GPU так, как это делает mainline Linux, — или пускает иначе.
  Стадия `fault` (линия 361) не исполнялась: она после маршрута.

Следующее (с согласия, человек рядом): строка в журнал между записью
S2CR и записью SMR — экран покажет, на какой из них встаёт; и линию 361
проверить без маршрута (FSRRESTORE от маршрута не зависит).

### Четвёртый круг: линия 361 работает; останавливает запись S2CR [T] (17:20–17:30)

Журналы — `probe-6.txt`…`probe-8.txt`; экран — `D:\minix\fotos\IMG_1133.jpg`.

- **Линия банка 0 работает** (`stage=fault route=0`, без маршрута):
  50 мс тишины, на `FSRRESTORE` TF — одно прерывание на INTID 361 (`fsr
  00000402`: TF и поле FORMAT), после очистки — тишина. Банк, таблицы и
  его линия — всё на железе.
- В одном прогоне Q17 сказал «cb0 differs», в следующем на том же старте —
  «all as found»; какой регистр отличался, первая версия Q17 не называла
  (теперь называет). Не разобрано, не повторилось.
- **Останавливает запись S2CR.** Строка перед записью — `Q15 writing s2cr
  0 = 00000000 (translate, cb0)` — на экране последняя; строки после неё
  (чтение S2CR и запись SMR) нет. Банк 0 к этому моменту настроен как у
  Linux (CBAR `0x1f300`, SCTLR `0x1167`), 7 трансляций прошли. Значит,
  дело не в порядке: гипервизор этой прошивки не принимает от NS-стороны
  S2CR «translate» вообще — ни в ненастроенный банк, ни в настроенный.
  Fault-тип он молча игнорирует, translate — останавливает машину.
- [?] Как тогда GPU получает маршрут: downstream (kgsl, `qcom,hyp_secure_alloc`,
  `qcom,retention`) — вероятно, через гипервизор/TZ своим вызовом, а не
  записью регистра; mainline на clover — проверить, работает ли там GPU на
  такой прошивке вообще. Следующий шаг — разбор, без записей на планшете.

## 16. Маршрут SID 0 → банк 0 работает [T] (2026-10-17, 19:15)

**Причина остановок — инструкция, а не порядок.** Дизассемблер службы:
обе записи S2CR, на которых вставал планшет, GCC сделал как `str wzr,
[x0, w19, sxtw]` — запись константы 0 из нулевого регистра (SRT = 31).
Запись «fault» `0x00020000`, которую гипервизор молча проигнорировал, шла
из обычного регистра. Регистры S2CR гипервизор перехватывает и
эмулирует по синдрому (ESR_EL2.SRT); регистр-источник 31 этот эмулятор,
по-видимому, не разбирает. Записи нулём в страницу банка 0 (MAIR1, TTBR1)
тоже были `wzr`/`xzr` и прошли — эти регистры не перехватываются.

**Исправление** (`adreno.h`): каждое обращение службы к регистру — одна
инструкция `ldr`/`str` в форме Linux (`asm "str %w0, [%1]"`), значение
всегда в обычном регистре (`"r"`, не `"rZ"`), адрес без смещения и записи
назад. S2CR пишется значением downstream-ядра этого планшета: translate,
банк 0, SHCFG NSH — `0x00000300` (`S2CR_ROUTE_CB0`).

[?] Изменились сразу две вещи — инструкция и значение (0 → `0x300`);
какая из них решила, не разделено. Запись `0x00000000` из обычного
регистра не пробовалась: она снова может стоить остановки.

**Круг** (`probe-10.txt`, `probe-11.txt`):

```
Q14 cb0: 7 of 7 translations as the tables say; fsr after them 400
Q15 writing s2cr 0 = 00000300 (translate, cb0)
Q15 s2cr 0 reads 00000300; writing smr 0 = 80000000
Q15 sid 0 by smr 0 to cb0: smr 80000000 s2cr 00000300; 10 ms: fsr 400 gfsr 0; taken back
Q16 line 361 quiet before; raised tf: it came, fsr 00000402 far 0 fsynr0 0
Q16 cleared (fsr 00000400), quiet after: the line works
Q17 smmu put back: smr 0 cb0; all as found
```

- Поток GPU направляется в банк 0 и снимается; за 10 мс ни отказа банка,
  ни глобального (GPU простаивает — обращений и не ждали).
- Линия 361 работает и с живым маршрутом.
- **Регистры SMMU переживают выключение GX:** второй прогон нашёл TCR
  банка 0 от первого (`00802010`) и S2CR 0 = `0x300`. Значит, «выключили
  питание — всё сбросилось» для этого SMMU неверно; возврат делает только
  служба. Оставшееся после круга состояние безопасно: SMR не VALID (ничего
  не направлено), банк 0 выключен и остаётся stage-1, S2CR на него
  указывает (fault-тип назад не записать).

Итог шага SMMU (§11, шаг 6, переставленный перед 5): таблицы, банк,
TLB, маршрут и линия отказа проверены на железе.

## 17. Zap и CP: первая команда GPU [T] (2026-10-17, 20:16–20:35)

Шаги 4 и 5 плана (§11). Код — `faabb6c7f` (ядро, libsys, RS),
`4f5d09bb5` (служба, стенд), `59d1dfe83` (ожидание CP).

**Образ.** Ядро `~/obj-kvar/gpu` (`kbuild-variant.sh gpu "-O2"`), архив —
`boot-gptro.mba` прошлых кругов, в котором заменён один модуль, RS
(`mba-swap.py -m rs`: новый RS в 139 112 байт лёг на место прежнего, 11
остальных образов побайтно те же) — `~/work-adreno-zap/clover/boot-zap.mba`.
Корень — eMMC, `cycle.sh` по умолчанию. Круги `20261017200922`,
`…202845`, `…203248`; журналы — `D:\minix\clover-flash\gpu\probe-12.txt`
… `probe-17.txt`. На карте памяти ядра видно: `0xf0b00000–0xf0b02000`
вырезан как `no-map`.

**Стадии** (каждая включает предыдущие; `gpu-probe.sh zap cp`):

| Шаг круга | Аргументы | Что делает | Вопросы |
|---|---|---|---|
| i | `stage=zap` | + zap: загрузка при включённом GPU, или resume | Q18 |
| j | `stage=cp` | + CP, zap после `CP_ME_INIT`, выход из secure mode, первая команда, остановка | Q19–Q23 |

| Q | Вопрос | Что решает |
|---|---|---|
| Q18 | разбор MDT; INIT_IMAGE, MEM_SETUP, копия сегментов и чтение назад, AUTH_AND_RESET — или SET_REMOTE_STATE | принимает ли TZ этот образ от MEECHO; путь zap целиком |
| Q19 | микрокод (слова 1 и 3 PFP), буферы GPU в банке 0; число записей `hw_init` и три регистра обратно | микрокод и раскладка; регистры пишутся |
| Q20 | `CP_ME_INIT`: rptr, тень WHERE_AM_I в памяти, простой | CP стартовал, читает микрокод и кольцо через SMMU, **пишет** через него |
| Q21 | `CP_SET_SECURE_MODE` 0: простой, `SECVID_TRUST_CNTL` | выход из secure mode |
| Q22 | `CP_SCRATCH_REG(2)` и `CACHE_FLUSH_TS` с прерыванием: линия 332, забор в памяти, FSR банка 0 | первая команда — та форма, в которой msm DRM будет отправлять задания |
| Q23 | маска прерываний, порты VBIF остановлены | ничего не в полёте, когда маршрут снимается и GX гаснет |

**Строки свежей загрузки** (`probe-17.txt`, первый `cp`):

```
Q19 cp: pm4 19568 bytes at 2000000 (005ff063 88020003), pfp 16140 at 2010000 (005ff08a 00087001), where-am-i; ring 2020000, memptrs 2030000
Q19 cp: hw_init, 186 writes; cp_protect_cntl 7 uche_cache_ways 2 pc_dbg_eco 80100000
Q20 cp: me_init done, idle in 110 us (read in 10, busy 80000100): rptr c wptr c shadow c, rbbm 00000001 int 00000000
Q18 zap: a512_zap.mdt 6684 bytes, 3 headers, hash 1 (6536 bytes), load 1, relocatable, split; image 5000..7000, 2000 bytes, state none
Q18 zap: init_image(13, 6684 bytes at f0f97000): ok
Q18 zap: mem_setup(13, f0b00000, 2000): ok
Q18 zap: segment 2 at +0: 4916 bytes + 0 zero, read back the same
Q18 zap: auth_and_reset(13): ok - authenticated; secvid 00000001
Q21 cp: set_secure_mode 0, idle in 560 us; secvid 00000000, rbbm 00000001 int 00000000
Q22 line 332 came: int 00100000, fence 1 in memory, scratch2 1, idle; smmu fsr 400
Q22 the GPU ran its first command: the line, the fence and the scratch register say so
Q23 cp: interrupts masked, vbif ports halted (f)
Q17 smmu put back: smr 0 (s2cr left 00000300) cb0 off, kept stage 1; all as found
Q11 undone: cx off gx off; all as found
```

Следующий `cp` той же загрузки: `Q18 zap: authenticated since boot;
set_remote_state(resume, 13): ok`, дальше те же строки. Всего на трёх
загрузках: zap загружен три раза (один раз стадией `zap`, без CP, — тоже
принят), возобновлён шесть раз; из восьми прогонов `cp` первая команда
прошла в семи (восьмой не дождался ME_INIT — ожидание, ниже). После
каждого прогона Q11 и Q17 — «all as found», планшет отвечает.

Что из этого следует:

- **TrustZone принимает zap от MEECHO.** Метаданные из памяти, которую
  ядро почистило; регион, выданный RS по `memory-region`; размер 0x2000.
  `PAS_IS_SUPPORTED(13) = 0` (§13) ничего не значил: Linux прав, что его
  не спрашивает.
- **AUTH_AND_RESET сам secure mode не снимает**: `secvid` после него —
  1. Снимает CP своей командой, и только после zap.
- **CP работает через SMMU в обе стороны.** Микрокод и кольцо он читает
  из памяти службы через банк 0, WHERE_AM_I и забор пишет туда же; FSR
  банка — только поле FORMAT (0x400), отказов нет.
- **После выключения GX GPU снова в secure mode** (Q6 следующего прогона:
  `secvid 00000001`), и каждый подъём — resume и `CP_SET_SECURE_MODE` 0,
  как у Linux после runtime suspend.
- **Линия 332** приходит на `CACHE_FLUSH_TS`; `RBBM_INT_0_STATUS`
  `0x00100000` — ровно бит 20.

**Ожидание CP [?].** Первая версия ждала rptr, `RBBM_STATUS` и
`RBBM_INT_0_STATUS` одним циклом, всеми тремя на каждом шаге. ME_INIT
после свежего подъёма тогда простаивал через 69 мс и 119 мс, а один раз
не дождался за секунду — при rptr = wptr и `RBBM_STATUS` 1 на печати
сразу после срока. Linux ждёт в два шага (`adreno_idle()`: rptr; затем
`_a5xx_check_idle()`: простой; по секунде). Так тот же ME_INIT: rptr за
10 мкс, простой за 100–110 мкс, пять прогонов из пяти. Почему совместный
опрос так тянул, не установлено (догадка — частые чтения `RBBM_STATUS` по
AHB, пока CP поднимается); порядок теперь Linux'а, а отказ по сроку
называет, дошёл ли rptr, когда, и какие биты `RBBM_STATUS` держались.

**Стенд** (`port/test/adreno`): `tzmodel.c` — учёт ядра (`PAS_STATE`,
порядок, `EALREADY`) и проверки TZ, которых на планшете нет вовсе
(метаданные — как их держит память, а не кэш; байты региона; GPU включён
и регион не отображён на AUTH); `cpmodel.c` — CP, который читает
микрокод и кольцо **через модель SMMU из памяти**, проверяет чётность
PM4, ME_INIT Linux'а и что secure mode снимается только после zap,
пишет назад и поднимает 332; значения регистров после `hw_init` — против
выписанных заново из `a5xx_gpu.c`. Настоящие файлы — если есть
`ZAP_DIR`/`FW_DIR`. 427 проверок; мутаций 59 из 59 (среди новых:
кольцо и микрокод не почищены, забор читается из кэша, нет чётности,
secure mode без zap, GX гаснет под работающим CP, CP без маршрута,
опечатка в HWCG, длины CP_PROTECT, потерянные биты read-modify-write,
регион отображён на AUTH, хвост сегмента не обнулён, нет MEM_SETUP,
хэш не дописан, повторная загрузка вместо resume). Модель SMMU теперь
запрещает освобождать под включённым банком только то, что SMMU
обходил или во что транслировал.

**QEMU:** `sys_qcomscm` — ENODEV, служба уходит словами; с новым RS
драйверы virtio получают свои права как прежде (`qemu-run.py`, 4 ядра).

**Что дальше — шаг 7, msm DRM.** Кольцо, забор, прерывание и выход из
secure mode уже есть; не хватает интерфейса для Mesa (§8): буферы GEM с
отображением в банке 0, `GEM_SUBMIT` в кольцо в форме `a5xx_submit()`,
`WAIT_FENCE` по прерыванию 332, разбор отказа SMMU в задании.

## 18. Шаг 7: msm DRM в службе [C] (2026-10-17)

Код: `1a46adcef` (общая часть узла — `minix/lib/libdrmdriver`, из
`mali_drm.c`) и `822ff9725` — служба `adreno`: `adreno_drm.c` (вызовы msm),
`adreno_gpu.c` (GPU, поднятый надолго, и кольцо), `adreno_cp.c` и
`adreno_smmu.c` разделены на подъём и обслуживание. Заголовок
`msm_drm.h` — копия Mesa (MIT) в `external/mit/libdrm/lib/libdrm/drm`.

**Как служба живёт.** Без `stage=` служба остаётся: проходит все стадии
§13–§17 (домены, SMMU, маршрут SID 0 → банк 0, zap, CP, первая команда)
и вместо «всё как было» объявляет узел:

```
/dev/dri/renderD128 is served (msm 1.6): A512, chip 0x05010200, gmem 0x44000 at 0x100000, buffers from 0x4000000
```

`minix-service down adreno` гасит GPU в обратном порядке (Q23, Q17, Q11)
— то же «all as found», что у стадий.

**Что отвечает.** Имя `msm`, версия **1.6**: softpin (1.4, адреса пишет
Mesa), очереди и их FAULTS (1.3, 1.5), sync objects в отправке (1.6).
SUSPENDS (1.7) и CACHED_COHERENT (1.8, это a6xx) не объявлены.
`GET_PARAM` — как `adreno_get_param()` Linux: GPU_ID 512, CHIP_ID из
дерева, GMEM 0x44000 по 0x100000, PRIORITIES 3, HIGHEST_BANK_BIT 14,
UCHE_TRAP_BASE, TIMESTAMP — счётчик ALWAYSON. MAX_FREQ — 19,2 МГц: GPU
пока идёт от кварца (шаг 10). VA_START/VA_SIZE отвечаются (у Linux на
a5xx `EINVAL`): `0x4000000`–`0x100000000`, ниже — буферы службы.

**Буферы.** Память — `libdrmdriver` (страницы службы, `vm_remap()`
клиенту, `mmap(2)` через `MIOCMMAP`, кэшируемая, чистка вокруг задания —
как у `mali`); `MSM_BO_WC` принимается как это. Адрес в банке 0 —
`GEM_INFO(GET_IOVA)`, первое подходящее место в пространстве клиента.
`GPU_READONLY` — отображение только на чтение. `MADVISE` — всегда
«retained»: служба ничего не выбрасывает.

**Своё адресное пространство у каждого клиента** — как у Mali на CB2, а
не одно на всех, как у Linux на a5xx: TTBR1 на этой SoC запрещён (§4), и
переключать таблицы кольцом нечем. Служба переключает их сама, между
заданиями: задание другого клиента ждёт, пока кольцо опустеет и GPU
встанет, банк 0 получает его таблицу (TTBR0, TLBIASID), и задание
начинается со сброса UCHE — того, что пишет `fd5_cache_flush()` Mesa
(`0x0e91`…`0x0e95`). Буферы службы (кольцо, микрокод, память записей
GPU) лежат в каждой таблице по одному адресу.

**Задание в кольце — в форме `a5xx_submit()` для одного кольца:**
`CP_SET_PROTECTED_MODE 0`, `CP_CONTEXT_SWITCH_SAVE_ADDR 0` (вытеснения
нет), `CP_SET_PROTECTED_MODE 1`, `CP_INDIRECT_BUFFER_PFE` на каждый
буфер команд, `CP_SCRATCH_REG(2)` = забор, `CP_EVENT_WRITE
CACHE_FLUSH_TS` с прерыванием. Буфер, на который ссылаются из другого
(`IB_TARGET_BUF`), в кольцо не идёт; буфер восстановления контекста —
только если прошлое задание кольца было чужим. Номер забора — свой у
каждой очереди; в кольце — сквозной номер службы.

**Заборы.** Линия 332: всё, чей забор в памяти, кончилось. `WAIT_FENCE`
— по номеру очереди, со сроком; срок прошёл — `ETIMEDOUT`, номер больше
выданного — `EINVAL`, как у Linux. `GEM_CPU_PREP` ждёт заданий на
буфере, с `NOSYNC` — `EBUSY`. Sync file и sync object на вход и на
выход, неявный порядок на общих буферах (кроме `NO_IMPLICIT`) — из
`libdrmdriver`.

**Отказы.** Линия 361 — отказ в пространстве клиента: банк не
останавливается (terminate), GPU идёт дальше, а задание, которое было
первым в кольце, кончается отказом — его sync file показывает `EIO`,
ожидание sync object — `EIO`, счётчики `MSM_PARAM_FAULTS` и FAULTS
очереди растут. `WAIT_FENCE` остаётся Linux'овым: 0, когда задание
кончилось, удачно или нет. Отказ ищется **до** заборов: линии приходят
порознь, и задание не должно кончиться удачно оттого, что линию забора
взяли раньше. Задание без движения `hang=` мс (2000) дважды подряд — или
собственное обнаружение зависания GPU — зависание: всё в кольце
отказывает, GPU выключается и поднимается заново (zap — resume), ждавшие
задания идут.

**Стенд** (`port/test/adreno`, `run.sh` собирает `adreno-test` и
`adreno-msmtest`). `msmtest.c` — клиент на модели планшета: точки входа
устройства вызываются, как их звал бы libchardriver, аргумент — по
гранту ровно того размера, что в номере запроса; команды — PM4, как у
Mesa. `osmodel.c` — система под `libdrmdriver` (гранты, `vm_remap`,
таймеры, `cdevfd`). `cpmodel.c` теперь исполняет кольцо и буферы
клиентов **через модель SMMU из ОЗУ**: `CP_INDIRECT_BUFFER_PFE`,
`CP_MEM_WRITE`, `CP_NOP`, `CP_WAIT_FOR_IDLE`, PKT4 в `CP_SCRATCH`, сброс
UCHE, `CACHE_FLUSH_TS`; запись в неотображённое — отказ в FSR банка и
линия 361. Модель памяти пишет назад только грязные строки, страницы
буферов разбросаны по физическим адресам, а освобождение проверяется
против всего, что SMMU ещё может достать. Что проверяется: ответы на
каждый вызов и коды ошибок Linux; форма задания в кольце; сброс UCHE при
смене клиента; заборы, `WAIT_FENCE`, sync file и sync objects; изоляция
(один адрес у двух клиентов — разные страницы, TLB после смены таблицы
чужого не помнит, задание другого клиента — только на стоящем GPU);
кэш до и после задания; отказ и следующее чистое задание; зависание и
жизнь после него; в конце — ничего не осталось, всё как было.
**204 проверки, 0 отказов**; прежний стенд стадий — 427. Мутаций
**96 из 96** (37 новых: rptr из регистра CP, задание под чужой таблицей,
смена таблицы без
TLBI, забор на один позже, запись GPU не показана процессору, отказ
забыт или взят после забора, `CACHE_FLUSH_TS` без прерывания, зависание
без восстановления, буфер дважды в отправке, буфер команд за своим
буфером, номер очереди отстаёт, `NOSYNC` ждёт, `EBUSY` вместо
`ETIMEDOUT`, буфер ушёл, оставшись в таблице или в TLB, и др.). Стенд
`mali` на библиотеке — как до выноса (76, 111, 287, 160; мутаций 83 из
83).

**Зонд на планшет** — `port/test/adreno/fdtest.c`: клиент узла без Mesa,
каждое слово проверяет процессор. `info` (версия, параметры, бег
TIMESTAMP), `job` (буферы; `CP_MEM_WRITE` в буфер; забор по
`WAIT_FENCE`, sync file; слова назад), `fault` (запись туда, где никто
не отображал: задание отказывает, FAULTS считает, следующее чистое),
`two` (два контекста, по буферу на одном адресе: таблицы переключаются,
каждый видит своё), `chain [n]` (n заданий подряд и время каждого).
Круг — стадия `drm` у `gpu-probe.sh` (`drm:ARGS` — аргументы зонду):
служба поднимается и остаётся, зонд спрашивает, служба уходит, новые
строки `kmsg`.

### На планшете [T] (2026-10-17, 22:21–22:45)

Комплект `D:\minix\clover-flash\2026-10-17` (ядро `obj-kvar/kit1017`,
архив и корень от дерева `822ff9725`; на QEMU проверены те же
`kernel.bin`, `boot.mba` и корень). С согласия пользователя хаб записал
корень в `userdata` (`fastboot flash`, 108 с) и загрузил
`meecho-clover-emmcnet.img`; по ssh — `c0d0`, новые `adreno` и RS. Круг —
`gpu-probe.sh drm`, журналы — `D:\minix\clover-flash\gpu\probe-drm-1.txt`
… `probe-drm-4.txt`.

**Первый прогон: задание не входило в кольцо.** `info` ответил как надо
(`msm 1.6.0`, 512, `0x05010200`, GMEM, TIMESTAMP 18,6 МГц от
ALWAYSON); тест отказа тоже (отказ посчитан, sync file `-5`, GPU пошёл
дальше, следующее задание чистое). А `job`, `two` и `chain` — таймаут
`WAIT_FENCE` за секунду. В журнале службы отказавшее задание — `job 2`:
номер 1 у первой команды, то есть задание теста `job` номера не получило
вовсе. Первое задание клиента ждёт смены таблицы, а смена — `cp_quiet()`.
Строка о том, что держит (добавлена этим кругом и осталась):

```
gpu: not idle for a switch of tables: rptr 1b wptr 1b, rbbm 800001c1 status3 00000000, ...
```

`RBBM_STATUS` `0x800001c1` при `rptr == wptr` — это `GPU_BUSY_IGN_AHB`,
`GPU_BUSY_IGN_AHB_HYST`, `CP_BUSY_IGN_HYST`, `CP_BUSY` (a5xx.xml):
**CP занят тем, что `cp_quiet()` прочитала его `CP_RB_RPTR` по AHB**
за мгновение до `RBBM_STATUS`, и так на каждом взгляде. `idle()` подъёма
ждёт в два шага (rptr, потом только статус) и поэтому проходит. Тест
отказа прошёл потому, что его задания — одного клиента: смены таблицы
нет. Linux берёт rptr из тени WHERE_AM_I (`a5xx_get_rptr()`); теперь и
`cp_quiet()` (каждое задание и так кончается WHERE_AM_I). Это же,
по-видимому, объясняет «Ожидание CP [?]» из §17: совместный опрос читал
rptr и статус подряд.

**После правки — 0 отказов:**

```
version: msm 1.6.0, 20130625, "MSM Snapdragon DRM"
gpu id 512, chip id 0x05010200
timestamp: 1858491 ticks in 100000 us (18.58 MHz)
wait_fence 1: Undefined error: 0 in 0 us
the target: words 16..31 cafe0000..cafe000f, the rest 5a5a5a5a; 0 words wrong
sync file: poll 1 (revents 1), status 1
a write to 0x7f000000: wait Undefined error: 0, faults 0 -> 1, the queue's 1
its sync file: status -5 (failed)
the write after the fault: 600d0001 (the GPU went on)
the next job: Undefined error: 0, 600d0002, the queue's faults 1
two contexts: their targets at 0x4000000 and 0x4000000
each its own: aaaa0001, bbbb0002 (Undefined error: 0, Undefined error: 0)
chain: 100 jobs, Undefined error: 0, 0 words wrong; a job and its wait 100 us on average, 10000 at worst
fdtest: 0 NOT as it should be
```

Служба: `fault at 7f000000 (fsr 00000402 fsynr0 00000011), the access
terminated; job 3 fails`; после `down` — Q23, Q17, Q11 «all as found»,
планшет отвечает. Итак, на железе: буфер в своём пространстве клиента,
`CP_MEM_WRITE` из буфера команд через банк 0, забор по линии 332,
`WAIT_FENCE`, sync file, отказ SMMU в задании и жизнь после него, два
клиента с одним адресом на разных страницах (смена TTBR0 и TLB), 100
заданий подряд — 100 мкс на задание с ожиданием (10 мс в худшем: тик).

**Модель стенда теперь это знает**: чтение `CP_RB_RPTR` занимает CP на
20 мкс (`CP_BUSY` и гистерезис, `800001c0`), дольше шага опроса службы.
Прежний `cp_quiet()` — мутация, её ловит `msmtest` (задание второго
клиента не входит в кольцо). Ожидания Q20/Q21 стенда стадий — «idle in
20 us, busy 800001c0», как на планшете («110 us, busy 80000100»).

`gpu/adreno` комплекта и `/service/adreno` на планшете — исправленная
служба; в образе корня — прежняя.

## 19. `glcube` на планшете: куб от Adreno через Mesa freedreno [T] (2026-10-17, 23:13–23:20)

Шаг 9 плана — раньше шага 8 и с двумя обходами (ниже). Mesa в DESTDIR
та же, что на CB2 (`6f6631ad8`: один `libgallium` с panfrost и
freedreno); `glcube` собран `port/test/gles/build.sh`, проверка
рендерера теперь принимает и Adreno (`FD512`). Служба — `afffa144…`
(§18), образ — `meecho-clover-emmcnet-1h.img` (тот же, что `emmcnet`,
сторож 3600 с), через очередь хранителя. Прогон —
`D:\minix\clover-flash\gpu\cube.sh` (служба, `glcube` в фоне, копия
`/dev/fb0` на 30-й секунде, служба вниз); журналы — `cube-1.txt` …
`cube-3.txt`, кадр — `cube-adreno.png` там же.

```
GL_VENDOR   freedreno
GL_RENDERER FD512
GL_VERSION  OpenGL ES 3.1 Mesa 26.2.4
ok   the middle of the first frame is the cube: 0xd5432d
ok   its corner is the clear colour: 0x101828
ok   frames 0 and 20 differ: it turns
GLCUBE 300 frames 512x512 in 120.91 s: 2.5 frames/s
GLCUBE 15 checks, 0 failed
```

Кадры/с — от GPU на кварце (19,2 МГц, шаг 10) и копии каждого кадра
процессором (`glReadPixels`, `/dev/fb0` — `simple-framebuffer`).

**Два обхода, оба — в окружении прогона, не в коде:**

- **`IR3_SHADER_DEBUG=nocache`.** Без него `glcube` падает в
  `ir3_disk_cache_init()`: ключ кэша шейдеров Mesa берёт из заметки
  build-id своего же образа (`build_id_find_nhdr_for_addr()`), у
  статической программы MEECHO её нет — NULL, `assert` в сборке выключен,
  чтение по адресу 4. Panfrost этим путём не ходит. Правка — компоновать
  программы с Mesa с `--build-id` (или кэш выключить в сборке Mesa).
- **`FD_MESA_DEBUG=serialc`.** Без него — переполнение стека в
  `handle_copies()` (`ir3_lower_parallelcopy.c`: `struct copy_ctx` — три
  массива по `RA_MAX_FILE_SIZE` на стеке) в потоке очереди компиляции.
  Mesa создаёт его `pthread_create(..., NULL, ...)`, а это
  `mthread_create` с умолчанием `libmthread` — `MTHREAD_STACK_MIN`,
  8 КиБ. `serialc` компилирует в главном потоке. Правка — у
  `pthread_create` без атрибутов стек как у POSIX-систем, а не как у
  служб.

## 20. Шаг 10: частоты GPU и экран планшета [C/T] (2026-10-17/18)

**Такт ядра от PLL** (`adreno_cc.c`, `cc_gfx3d_set()`; Linux —
`clk_gfx3d_src_ops`, `gpucc-sdm660.c`, `clk-alpha-pll.c`). Каждая новая
частота строится на том PLL, от которого корень `gfx3d` сейчас не
питается («пинг-понг»): выключенному PLL пишутся L, alpha (40 бит,
дробь в 32 старших с округлением вверх), полоса VCO (1000–2000 МГц — 0,
500–1000 — 2, 250–500 — 3), `CONFIG_CTL` Linux'а `0x4001055b`, `ALPHA_EN`
и `PLLOUT_MAIN`; включение — `BYPASSNL`, 5 мкс, `RESET_N`, ожидание
`LOCK_DET` (до 1,5 мс), `OUTCTRL`; затем CFG корня с источником и
делителем и `UPDATE`; прежний PLL гасится. PLL работает на удвоенной
частоте (для 160 МГц — на учетверённой: 640/4). Работающий PLL не
пишется никогда — у «default» alpha PLL нет динамической смены.
`cc_power_down()` возвращает корень на кварц, гасит PLL и пишет их
регистры как были; Q11 сверяет и PLL.

**Планшет, 160 МГц** [T]: `Q24 gfx3d 160 MHz: pll0 at 640 MHz (L 21,
alpha 5555555600, band 2) locked in 8 us, the root on it /4; was 19 MHz`;
после `down` — «all as found». Загрузчик оставляет PLL0 на 1176 МГц
(L 0x3d), PLL1 на 640 (L 0x21) — ровно таблица Linux, оба выключены.

**Выбор частоты** (`adreno_gpu.c`, `freq=МГц` или `freq=max`): точка из
`opp-table` дерева, которую разрешает бин чипа, и голос VDDMX за её
`opp-level` — в порядке OPP Linux'а: при подъёме сначала голос, потом
такт; при снижении — наоборот; при выключении GPU голос снимается
(`swen` 0, как `rpmpd_power_off()`). VDDMX кроме GPU никто не голосует
(дисплей — CX, `sdm660-xiaomi-clover-common.dtsi`).

- **Бин** — фьюз `qfprom@780000`, ячейка `gpu-speed-bin` (`reg <0x41a2
  2>`, `bits <5 8>`), через `nvmem-cells` узла GPU; страница qfprom
  отображается только на чтение (`devicetree "qcom,qfprom"`), такт не
  нужен. Фьюз → бит `opp-supported-hw` по каталогу Linux (A512): 0 и 157
  → 6, 146 → 5, 135 → 4, 122 → 3, 90 → 2, 78 → 1; неизвестный — бит 0
  (точки всех бинов, до 370 МГц). Строка Q26.
- **Голос** — через службу `qcom_rpm`: запрос `QRPM_SET`
  (`<minix/qcom_rpm.h>`), только ключи `vlvl` и `swen` рельсов CX и MX,
  активный набор; `qcom_rpm` после старта больше не молчит, а отвечает.
  Права — `ipc … adreno` в её блоке `system.conf`. Строка Q25.
- Без `qcom_rpm` — только точки уровня MIN_SVS (160 МГц): ниже его
  рельс не бывает, раз всё работает.

**Стенд** (`port/test/adreno`): модель знает PLL — захват только при
конфигурации Linux'а и частоте в полосе VCO, не раньше 5 мкс после
`BYPASSNL`; запись в работающий PLL, корень работающей ветви на
неработающем источнике, остановленный под ядром PLL — нарушения;
частота ядра — только из таблицы Linux и не выше того, что держит голос
VDDMX (без голоса — 160); голос, снятый под ядром, которому его мало, —
нарушение, оставленный к концу — «не как было»; qfprom с фьюзом из
сценария. `msmtest` `t_clock`: 160 и «пинг-понг», 266 с голосом, `max`
→ 588 на бине 0, бин 122 → потолок 465, без `qcom_rpm` — только 160.
427 + 242 проверки; мутаций 111, из них 15 — частоты (без пинг-понга,
корень до захвата, погашен не тот PLL, делитель, полоса VCO, PLL не
возвращены, сброс без паузы после обхода, конфигурация, `ALPHA_EN`,
выход PLL, такт выше голоса, голос не снят, бин не учтён, таблица бинов
со сдвигом, биты фьюза без сдвига).

**Узкое место было не в GPU** [T]. `glcube` на экране — 2,5 кадра/с и
на 19,2, и на 160 МГц; без экрана — 136 и 286 кадров/с (512×512 с
чтением назад), 128×128 — 750 и 1000. Время уходило в службу `fb`: на
планшете экран — кадр загрузчика (`simple-framebuffer`), и каждый
`FBIOC_FLIP` переписывал его целиком, 9,2 МБ записями в отображение
Device-nGnRnE, где каждая запись ждёт ответа: ~400 мс. Теперь
(`730299c30`): копируются только строки, которые могут отличаться
(повреждённые `y0..y1` плюс то, что сменилось на экране с прошлого показа
этого буфера), и только 16-байтные куски, отличные от копии экрана в
обычной памяти. 2,5 → 8,9 (только строки) → **48,3 кадра/с** (копия
экрана). `fblog` теперь читает сам экран назад и сверяет построчно:
«every row as buffer N» во время `glcube` и после возврата консоли.
Отобразить кадр загрузчика как write-combine (так делает `simplefb`
Linux) — дело VM, не сделано.

**Планшет, все точки** [T, 2026-10-18 00:56, `freqsweep.sh`, журналы
`D:\minix\clover-flash\gpu\freqsweep-1.txt`, `-2.txt`]: **бин — фьюз
135**, `opp-supported-hw 0x10`, точки до 588 МГц. `qcom_rpm` передаёт
голос (`rwmx enable 1`, `rwmx level N`), RPM его принимает; после `down`
— `enable 0`, «all as found».

| freq | VDDMX | PLL0 | `glcube` без экрана | на экране | `fdtest chain 300` |
|---|---|---|---|---|---|
| 19,2 | — | — | 136 | — | — |
| 160 | 48 (MIN_SVS) | 640, /4 | 337 | 48,0 | 200 мкс |
| 266 | 64 (LOW_SVS) | 532, /2 | 328 | 48,5 | 233 мкс |
| 370 | 128 (SVS) | 740, /2 | 343 | 48,4 | 166 мкс |
| 465 | 192 (SVS_PLUS) | 930, /2 | 353 | 48,3 | 200 мкс |
| 588 | 256 (NOM) | 1176, /2 | 343 | 48,7 | 200 мкс |

Захват PLL — 19–22 мкс. Выше 160 МГц куб 512×512 от частоты не зависит:
кадр стоит процессору (Mesa, чтение назад, кэши), на экране — копии в
кадр загрузчика. Чтобы увидеть сам GPU, нужна нагрузка тяжелее куба.

**Открыто:** `/sbin/reboot` из этого образа дошёл до fastboot через 24
минуты (00:23 → 00:46:54), хотя раньше — за секунды; причина не
выяснена (новые `fb` и `system.conf` в сброс не вмешиваются).
