# Upstream-серии из kernel.patch (SM6225 / Khaje)

Разбиение `kernel.patch` на логически независимые серии. Порядок —
от наименее спорных (generic fix) к платформенным (Khaje quirks).
Поведение не меняется — это организация для отправки наверх.

## Серия 1 — generic fixes (не SM6225-специфичные)

1. **`ath10k/wmi.c`: guard против freq==0** — драйвер падал/логировал
   мусор, когда firmware отдаёт нулевую частоту канала. Однострочный
   guard. Готов к отправке; проверен на железе (Wi-Fi 6GHz AP
   поднимается и даёт ~1170 Mbit).
2. **`clk`: orphan-reparent fix** (`clk.c` `clk_core_reparent_orphans`):
   orphan не имеет старого parent — hardware dance
   `__clk_set_parent_before/after` вызывал spurious «already
   disabled/unprepared» WARNs (наблюдались на DSI PLL на SM6225).
   Заменён на propagate состояния + обновление топологии.
   Cf. upstream f8f8f1d04494 + follow-ups — возможно уже решено
   наверху, сверить перед отправкой.
3. **`qcom_scm.c`: QSEECOM allowlist для `xiaomi,fog`** — UEFI
   secure-app трастлет на этом устройстве отвечает, efivars читаются.
   Платформенная добавка в существующую таблицу — простая.
4. **`gpucc`: SM6375 Lucid→Zonda silicon correction** — wrong GPU CC
   silicon variant для SM6225. Малая правка таблицы.

## Серия 2 — CAMSS SM6225 (задняя камера)

5. **`camss` SM6225 CSIPHY 3-phase tables + wiring**:
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

6. **`pinctrl-khaje` + `reserved_gpios`/`irq_quiesce`**:
   новый pinctrl-драйвер/вариант для SM6225 + два обязательных
   quirk'а: `reserved_gpios` маска (sparse TLMM-банки на Khaje
   вешают шину при доступе — gpio88/SD detect среди них) и
   quiesce прерываний до регистрации summary IRQ.
   Проверено: без reserved_gpios доступ к EAST-банку = hard hang.
   `irq_quiesce` оставлен — управляемого эксперимента не было.
   Серия = driver + DT-изменения pinctrl (идут вместе).

## Серия 4 — UFS на Khaje

7. **UFS Khaje quirks + reference-clock handling + DT**:
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

8. **IPA PAS memory setup / PIL sequencing**:
   `qcom_scm_pas_shutdown` ret-обработка в ipa_main (возврат не
   затирался) + region/layout правки PAS для SM6225. Зависит от
   memory carveouts в DTS. Проверено: LTE через IPA v4.2/QMAP.

## Серия 6 — SM6225/Xiaomi DT (последней)

9. **`sm6225.dtsi` + `sm6225-xiaomi-fog.dts`** — SoC-ноды + доска:
   reserved-memory carveouts (firmware regions — обязательны для
   UFS/remoteproc стабильности), pinctrl-маппинг, UFS/USB/DSI/camss/
   audio/BT-ноды, PMIC-ребёнки. Идёт последней — зависит от всех
   драйверных серий выше; разрезать на под-коммиты по подсистеме.

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
