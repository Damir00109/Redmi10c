# Upstream-серии из kernel.patch (SM6225 / Khaje)

Разбиение `kernel.patch` на логически независимые серии. Порядок —
от наименее спорных (generic fix) к платформенным (Khaje quirks).
Поведение не меняется — это организация для отправки наверх.

> **Сверено с `kernel.patch` 2026-10-19** (92 файла). Ниже перечислены не
> все файлы, а логические серии; состав проверялся грепом по диффу.

## Серия 1 — generic fixes (не SM6225-специфичные)

1. **`ath10k/wmi.c`: guard против freq==0** — драйвер падал/логировал
   мусор, когда firmware отдаёт нулевую частоту канала. Однострочный
   guard. Готов к отправке; проверен на железе (Wi-Fi 6GHz AP
   поднимается и даёт ~1170 Mbit).
2. **`clk`: crash при переселении детей в orphan** (2026-10-20) —
   `clk_unregister()` переселяет детей умирающего клока в orphan-список
   через `clk_core_set_parent_nolock(child, NULL)`, а `__clk_set_parent()`
   всё равно прогонял enable/disable-танец миграции состояния. Мигрировать
   некуда (железо не перепрограммируется, нового родителя нет), поэтому
   счётчики уже уходящего родителя декрементировались и в dmesg летели
   `WARNING: drivers/clk/clk.c:1188 at clk_core_disable` +
   `:1047 at clk_core_unprepare` с полным call trace. Воспроизводилось
   каждым рестартом ADSP (unregister клоков q6afe). Фикс: при `parent == NULL`
   делать только `clk_reparent()`. Проверено на fog: два цикла stop/start
   ADSP — ноль WARN/Oops.
3. **`q6dsp-lpass-clocks`: Oops при рестарте ADSP** (2026-10-20) —
   `q6dsp_clock_dev_probe()` выставлял `dev_set_drvdata()` **после** цикла
   `devm_clk_hw_register()`, а регистрация клока запускает
   `clk_core_reparent_orphans()` → `__clk_set_parent_before()` →
   `clk_q6dsp_prepare()` → `dev_get_drvdata()` == NULL → `cc->desc` → Oops
   (`pc : clk_q6dsp_prepare+0x20/0x40`). На первом пробе orphan'ов с таким
   родителем ещё нет, на ре-пробе `q6afe` после рестарта ADSP — есть.
   Фикс: публиковать drvdata до регистрации клоков. Проверено там же.
4. **`clk`: orphan-reparent fix — В ТЕКУЩЕМ ПАТЧЕ ОТСУТСТВУЕТ**
   (сверено с `kernel.patch` 2026-10-19: диффа `drivers/clk/clk.c` нет).
   История: правка `clk_core_reparent_orphans` (замена hardware-dance
   `__clk_set_parent_before/after` на propagate состояния + обновление
   топологии) была написана против spurious WARN'ов DSI PLL, но в
   актуальное дерево не входит. Из clk-правок остался только
   **`clk-bulk.c`** — `pr_err` → `pr_debug` для «Failed to prepare clk»,
   потому что опциональные клоки ADSP легально отдают `-ETIMEDOUT` до
   подъёма DSP и вызывающий всё равно уходит в deferred probe.
   **Пробел, если возвращаться к теме**: пропагация состояния на нового
   родителя делается только при reparent'е и не зеркалится на
   unprepare/disable, из-за чего при перезапуске ADSP возможен
   `WARNING ... Enabling unprepared LPASS_*_MCLK`. На текущем дереве
   WARN **не воспроизводится** (в dmesg за бут — 0 совпадений); прежде
   чем чинить, нужно воспроизвести рестартом ADSP. Правку «prepare перед
   enable» пробовали и откатили — ручной stop/start ADSP после неё вешал
   систему.
5. **`power: freezable poll work`** (2026-10-18) — опрос зарядника
   (`smb1351_charger_bringup`) и гейджа (`sh366101_fg_bringup`) шёл на
   незамораживаемой `system_wq`, из-за чего во сне делались i2c-транзакции
   (`i2c-3: Transfer while suspended` WARNING). Оба переведены на
   `system_freezable_wq` — generic-фикс, кандидат наверх.
6. **`qcom_scm.c`: QSEECOM allowlist для `xiaomi,fog`** — UEFI
   secure-app трастлет на этом устройстве отвечает, efivars читаются.
   Платформенная добавка в существующую таблицу — простая.
7. **`gpucc`: SM6375 Lucid→Zonda silicon correction** — wrong GPU CC
   silicon variant для SM6225. Малая правка таблицы.

## Серия 2 — CAMSS SM6225 (задняя камера)

8. **`camss` SM6225 CSIPHY 3-phase tables + wiring**:
   `lane_regs_sm6225` (3ph 1.2.1, отличие от sm8250 в одном регистре
   на линию), `*_res_sm6225` таблицы ресурсов, `qcom,sm6225-camss`
   compat. Зависимость: `assigned-clocks` AXI в DTS (камера-часть DTS
   — платформенная, идёт в серию 5). Проверено: полный стрим RAW10
   4080x3072.
   Известное отклонение от upstream, НЕ для отправки как есть:
   снятый `CLK_SET_RATE_PARENT` на camss-RCG (hard lockup при
   STREAMON — propagaция в живой PLL gpll0/gpll10). Для upstream
   нужен правильный фикс (разделяемые PLL fixed-ops, как downstream).

## Серия 3 — pinctrl Khaje

9. **`pinctrl-khaje` + `reserved_gpios`/`irq_quiesce`**:
   новый pinctrl-драйвер/вариант для SM6225 + два обязательных
   quirk'а: `reserved_gpios` маска (sparse TLMM-банки на Khaje
   вешают шину при доступе — gpio88/SD detect среди них) и
   quiesce прерываний до регистрации summary IRQ.
   Проверено: без reserved_gpios доступ к EAST-банку = hard hang.
   `irq_quiesce` оставлен — управляемого эксперимента не было.
   Серия = driver + DT-изменения pinctrl (идут вместе).

## Серия 4 — UFS на Khaje

10. **UFS Khaje quirks + reference-clock handling + DT**:
   `qcom,vddp-ref-clk-supply` в ufs-qcom (без него PHY не линкуется),
   QMP v4 PHY support для SM6225, DT-нода `4804000.ufshc`+`4807000.phy`.
   Задокументированный fallback: `qcom,disable-runtime-pm` (ahit=0 +
   pm_runtime_forbid) — без него гонка hibern8-exit ↔ devfreq scale
   вешает очередь (воспроизводимо, root-caused; devfreq сам по себе
   работает). `qcom,force-legacy-dme-quirks` УДАЛЁН — не нужен.
   НЕ upstreamable: `snps,dis-64bit-dma`+клэмп DMA-маски DWC3 (32-бит
   DRAM на Khaje, контроллер не за SMMU) — это отдельная платформенная
   правка, держать вне серии UFS.

## Серия 5 — IPA SM6225

11. **IPA PAS memory setup / PIL sequencing**:
   `qcom_scm_pas_shutdown` ret-обработка в ipa_main (возврат не
   затирался) + region/layout правки PAS для SM6225. Зависит от
   memory carveouts в DTS. Проверено: LTE через IPA v4.2/QMAP.

## Серия 6 — SM6225/Xiaomi DT (последней)

12. **`sm6225.dtsi` + `sm6225-xiaomi-fog.dts`** — SoC-ноды + доска:
   reserved-memory carveouts (firmware regions — обязательны для
   UFS/remoteproc стабильности), pinctrl-маппинг, UFS/USB/DSI/camss/
   audio/BT-ноды, PMIC-ребёнки. Идёт последней — зависит от всех
   драйверных серий выше; разрезать на под-коммиты по подсистеме.
   **USB здесь — чистая DT-правка** (2026-10-18): нода-обёртка
   `usb@4ef8800` (`qcom,sm6115-dwc3` + `qcom,dwc3`) с клоками, ресетом и
   `qcom,select-utmi-as-pipe-clk` вокруг `snps,dwc3`. Драйверные правки не
   нужны: upstream `dwc3-qcom` сам делает QSCRATCH/клоки/ресет, а свойство —
   штатный механизм для HS-only контроллеров (без него bind падает с
   `failed to enable ep0out`).
   **Depth-камера (2026-10-18)**: нода `ov02b1b` включена и связана портом с
   `camss port@1` (`csiphy1_ep` ↔ `ov02b1b_ep`, 1 линия, 332.8 МГц) — идёт в
   DT-серию вместе с остальными сенсорами.

## Серия 7 — QUPv2 I2C DMA quirk

13. **`i2c-qcom-geni`: `qcom,no-dma` DT property** — новый булев
    property, пиннит шину к FIFO-режиму (SE-DMA остаётся для
    GPI/hub-вариантов). Root cause на железе: на Khaje любой transfer
    ≥32 байта уходит в SE-DMA (`i2c_get_dma_safe_msg_buf(msg, 32)`) и
    **никогда не завершается** — шина теряется, задача уходит в
    D-state, система ловит hung-task. FIFO-путь работает штатно.
    Само property — generic-фича драйвера (кандидат наверх); его
    применение в `sm6225-xiaomi-fog.dts` (`&i2c1`) — платформенный
    workaround, пока не найден настоящий фикс SE-DMA.

## Серия 8 — новые драйверы (написаны с нуля)

14. **`power/supply/sh366101_fg_bringup.c`** — SinoWealth SH366101:
    SBS-свойства + gaugeinfo-блоки (Qmax→SOH, энергия, OCV, внутр. темпа),
    `system_freezable_wq`. Публикуемый драйвер, стиль upstream.
15. **`power/supply/smb1351_charger_bringup.c`** — Summit SMB1351 (зарядник).
16. **`gpu/drm/panel/panel-xinli-ft8006s.c`** — панель FT8006S.
17. **`media/i2c/ov02b1b.c`** — depth-сенсор (1 линия, 1600x1200).
18. **`misc/aw87xxx-mini.c`**, **`misc/wl2866d.c`** — PA Awinic и camera-PMIC
    (урезанные, «mini» — для наверх переписывать по подсистемам).
19. **`pinctrl/qcom/pinctrl-khaje.c`** — идёт в серию 3 (pinctrl).
    `input/touchscreen/fts_spi/` — вендорный форк, наверх не отправлять.

## Сознательно НЕ upstreamable (платформенные quirks — держать локально)

- hci_qca: bt_en-ограничение до wcn6750+, IBS off для wcn399x,
  msleep-тайминги, preshutdown-policy (upstream тоже шлёт preshutdown —
  мы вернули его поведение).
- icc-rpm `usb_axi` keep-alive на snoc+cnoc — платформенный
  hard-hang-workaround (root-caused на железе 2026-10-17).
- FTS SPI-драйвер focaltech (вендорный форк, не mainline-style).
- `reserved_gpios`/`irq_quiesce` — часть серии 3 как quirk-аргумент,
  но сам механизм upstream-согласуем.
- UFS `qcom,disable-runtime-pm` в DT — workaround документирован,
  не кандидат на удаление до фикса hibern8↔devfreq гонки.
- `clk_ignore_unused`/`pd_ignore_unused` в cmdline — bring-up
  костыль; платформа без них грузится (проверено), но Ubuntu-путь
  не валидирован — отдельный будущий эксперимент.
- `qcom,no-dma` на `&i2c1` в `sm6225-xiaomi-fog.dts` — обход
  зависания SE-DMA на Khaje (≥32 байт за transfer); механизм
  (property) upstream-согласуем, само применение — workaround
  до фикса DMA-пути.
