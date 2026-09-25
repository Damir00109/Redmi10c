# GPU (Adreno 610) bring-up — ГОТОВО

Дата: 2026-09-25. **Статус: GPU полностью работает.**

## Итог (проверено на устройстве)

```
adreno 5900000.gpu: драйвер стартовал                  ✓
msm_dpu: bound 5900000.gpu (ops a3xx_ops)             ✓
[drm] Initialized msm 1.13.0 on minor 0               ✓
adreno_request_fw: loaded qcom/a630_sqe.fw            ✓
adreno: mdt qcom/sm6225/a610_zap.mdt: relocate=1      ✓ ZAP-шейдер
/dev/dri/card0 + renderD128                           ✓
devfreq 5900000.gpu, governor=simple_ondemand         ✓
available_frequencies = 320/465/600/785/1025/1114.8 МГц ✓
cur_freq = 320 МГц (idle)                             ✓

gpucc_pll0            = 930 МГц   (L=0x30)
gpucc_pll0_out_main   = 465 МГц   (postdiv /2)
gpucc_gx_gfx3d_clk_src= 465 МГц
gpucc_gx_gfx3d_clk    = 465 МГц

dmesg errors по GPU: нет (остались только psci/Bluetooth — не GPU)
```

## Ключевые находки (по порядку)

1. **gpucc для khaje — свой** (вендор `qcom,khaje-gpucc`, `drivers/clk/qcom/gpucc-khaje.c`):
   - **PLL0 = ZONDA** (не Lucid/Fabia/Default), выход — **`OUT_MAIN`** (postdiv /2):
     ```c
     .l = 0x21, .alpha = 0x5555,
     .config_ctl_val = 0x08200800, .config_ctl_hi_val = 0x05022001,
     .config_ctl_hi1_val = 0x00000010, .user_ctl_val = 0x01000101,
     vco {595200000, 3600000000, 0}
     ```
   - PLL1 — Lucid; `parent_map_1` = `{TCXO, PLL0_OUT_MAIN, PLL0_2X_DIV, PLL1_EVEN, PLL1_ODD, GPLL0}`;
     `ftbl_gfx3d` = `P_GPU_CC_PLL0_OUT_MAIN, div 1`, 320…1260 МГц.
2. **compatible-ловушки**: mainline-драйверы матчат только свои compatible
   (`sm6115-gpucc`, `sm6375-dispcc`), DTSI использовал `sm6225-*` → драйвер не биндился.
3. **`clock-names` у gpucc обязателен** (`bi_tcxo`, `gcc_gpu_gpll0_clk_src`,
   `gcc_gpu_gpll0_div_clk_src`) — иначе PLL не находит родителя («Rounded rate 0»).
4. **postdiv `OUT_MAIN` надо ЗАРЕГИСТРИРОВАТЬ в таблице клоков драйвера** и оставить
   **`ro_ops`** (не settable!): settable `divider_determine_rate` не пересчитывает
   родителя → PLL0 получал 28.8 МГц (L=1) вместо 930 МГц и не лочился.
   `ro_ops` корректно ставит `best_parent_rate = rate × post_div`.
5. **UBWC**: для `qcom,sm6225` — запись с `highest_bank_bit = 13` (фьюз GPU).
6. **ZAP — формат MDT**: `a610_zap.mdt` + `.b00/.b01/.b02` (из Android
   `/vendor/firmware/`) в `qcom/sm6225/`. MBN не подходит.
7. `msm_mdss`: ICC-путь `mdp0-mem` сделан опциональным (иначе probe -22).

## Изменения в дереве

- **config**: `DRM=y`, `DRM_MSM=y`, `SM_GPUCC_6375=y`, `SM_DISPCC_6375=y`, `SM_GCC_6375=y`
- **sm6225.dtsi**: включены `gpu`/`gpucc`/`gmu`/`adreno_smmu`/`mdss`/`dispcc`;
  `gpucc` → `sm6375-gpucc` + `clock-names`; `dispcc` → `sm6375-dispcc`;
  убран `HLOS1_VOTE_GPU_SMMU_CLK`; `opp-supported-hw = <0x1f>` в `gpu_opp_table`
- **sm6225-xiaomi-fog.dts**: `&gpu`/`&mdss`/`&dispcc` → okay (`&mdss_dsi0*` disabled)
- **gpucc-sm6375.c**: PLL0 → ZONDA + khaje-конфиг; добавлен postdiv `gpucc_pll0_out_main`
  (ZONDA, `ro_ops`) и зарегистрирован в `gpucc_sm6375_clocks[]`
- **ubwc_config.c**: `qcom,sm6225` → `sm6225_data` (hbb=13)
- **msm_mdss.c**: ICC `mdp0-mem` опционален
- **initramfs**: `qcom/a630_gmu.bin`, `qcom/a630_sqe.fw`,
  `qcom/sm6225/a610_zap.{mdt,b00,b01,b02}`

## Образы

- **`out/boot-GOOD8-gpu-full.img`** (`15b0112e…`) — **GPU полностью работает**
- `out/boot-GOOD7-gpu.img` (`41cdd158…`) — GPU поднимался, но OPP падал
- звуковые: `boot-GOOD{,2,3,4,5,6}` (GOOD6 = `c6da05c3…`)

## Таблица частот (исправлено 2026-09-25)

Раньше OPP-таблица и `ftbl_gpucc_gx_gfx3d_clk_src` содержали
`820/980 МГц`, которых железо не выдаёт, и не содержали `1025/1114.8`.
Это давало `devfreq ... Couldn't update frequency transition information`.
Приведено к вендорному `/proc/device-tree/soc/gpu-opp-table` (A610v2,
speed-bin-1): **320 / 465 / 600 / 785 / 1025 / 1114.8 МГц**
(+1260 в bin0). Важно: 1114.8, а не 1100 МГц.

Также убран `opp-supported-hw` из `gpu_opp_table`: эта платформа не
вызывает `dev_pm_opp_set_supported_hw()` (нет nvmem speed-bin), а OPP-core
в таком случае **отключает** любой OPP с этим свойством — старое
`<0x1f>` на `opp-320000000` молча выбрасывало уровень 320 МГц.

## Что можно дальше (не обязательно)

- Проверить реальную работу GPU (нужен userspace: kmscube/glmark2, либо mesa).
- Дисплейная часть: DSI-панель (сейчас `mdss_dsi0*` выключены; экран — simplefb).
