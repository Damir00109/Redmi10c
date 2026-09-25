# Audio bring-up: Android-side capture + Linux bisect

## Цель
Получить полную картину управления аудио на stock Android, сравнить с
нашим mainline-прототипом и найти, где ошиблись.

## Что известно (Linux side)

### Зависание (hard lockup CPU)
- Репродукция: SND=y + любой LPASS-макрос enabled → ~36s после APR
  регистрации CPU жёстко виснет (NMI не обрабатывает), watchdog пишет
  `hard LOCKUP on cpu N`, модем падает через ~40с (`dog_halt_common.c:17`,
  "Q6 DSP detects stalled initialization").
- WQ-dump: `in-flight: pm_runtime_work_func` ×2; pending `smb1351_poll_work`,
  `sh_poll_work`, `deferred_probe_timeout_work` → geni_i2c таймауты как
  следствие.
- Стабильно без зависа: SND=y со ВСЕМИ LPASS-нодами off (913s+),
  lpi+CCs (90s+), только va_macro (dождался >100s без виса — см. ниже).
- Ставки подозреваемых: rx_macro (defer на fsgen, но вис был и так),
  swr0/swr1 (irq 296/297, reset через CC), q6afecc clock RPC к ADSP,
  iommu на q6asmdai (`iommus = <&apps_smmu 0x1c1 0x0>`).

### Проверено рабочее
- ADSP/modem стартуют, APR сервисы 4:3/4:4/4:7/4:8 регистрируются.
- aw87xxx-mini: chipid 0x76, profile 'music' applied (драйвер
  восстановлен из .o 1-в-1, лежит drivers/misc/aw87xxx-mini.c).
- lpi pinctrl: требует clock-names="audio" (исправлено в dtsi).
- wcd937x: `Error applying setting` → EINVAL на pinctrl-состоянии;
  `output-high` не поддерживается msm pinconf (PIN_CONFIG_LEVEL → EINVAL).
  Убрано output-high из wcd_reset state.

### Открытые вопросы
- Какой probe висит CPU? Может быть: MMIO к выключенному LPASS-блоку,
  APR-RPC без ответа, SMMU fault storm, shared-clock.
- Правильный ли `qcom,dmic-sample-rate`/slew/reg у макросов для khaje.
- Реальный путь звука: wcd937x RX (AUX/LO?) → aw87xxx на I2S/SDW?

## Android capture plan (adb, без прошивки)

### A. Пассивные дампы
- [x] Card0 = `bengal-idp-snd-card` (вендорный machine driver), 2509 контролов
- [x] mixer dump idle → `capture/mix_idle.txt` (asoc-dump — свой бинарь,
      /data/local/tmp/asoc-dump, тянет enum-имена)
- [x] dmesg idle → `capture/dmesg_idle.txt`
- [x] aw87xxx sysfs: `/sys/bus/i2c/devices/0-0058/{reg,awrw,profile,hwen,...}`
      → дамп `capture/aw87_idle.txt`; profiles: Music Voice **Bypass** Fm
      Receiver Off — **Bypass отсутствует в нашем mini-драйвере!**
- [x] PA на шине `i2c@4a84000` — той самой, что таймаутила при висе
- [x] drv_ver: v2.0.0.10, hwen_status: invalid (нет hw-ctrl GPIO?)
- [ ] find прочие sysfs: rx/vbat/monitor_* привязки к ASoC

### B. Активная сессия (музыка на динамик) — СНЯТО
- [x] PCM активен: `/proc/asound/card0/pcm0p` RUNNING, MultiMedia1,
      48kHz S24_3LE stereo, period 1920 → `capture/pcm_hw.txt`
- [x] mixer diff idle→play → `capture/mix_{idle,play}.txt`:
      - `RX_CDC_DMA_RX_1 Audio Mixer MultiMedia1` = 1 ← поток идёт в
        **codec-DMA RX_1 wcd937x** (данные по SoundWire), не через RX-macro DAI
      - `RX_MACRO RX2 MUX` = AIF2_PB
      - `RX INT2_1 MIX1 INP0` = RX2
      - `AUX_RDAC Switch` = 1 (аналоговый выход AUX → внешний PA)
      - `AUX PATH Mode` = HP_MODE
      - `RX_CDC_DMA_RX_1 Format` = S24_3LE
      - `aw87xxx_spk_switch`/`rcv_switch`/`profile_switch_0` = Music
- [x] aw87 regs diff idle→Music: 0x01: 00→07 (EN), 0x59: 92→4A,
      0x60: CA→9C → `capture/aw87_{idle,play}.txt`
- [x] vendor monitor loop: aw87390 читает батарею каждую сек,
      `aw_qcom_read_data_from_dsp` → vmax через APR (smartPA защита)
- [x] profile list вендора: Music Voice **Bypass** Fm Receiver Off
      (chip = **aw87390**, drv v2.0.0.10, hwen=invalid — нет hw GPIO)
- [x] SDW enumeration: `swr0`=VA(0xa740000) `swr1`=RX(0xa610000),
      wcd937x-slave.a01170223 (TX) на swr0, .a01170224 (RX) на swr1
- [x] vendor sound: model=`bengal-idp-snd-card`,
      `qcom,rxtx-bolero-codec`, routing `IN3_AUX AUX_OUT` +
      `SpkrMono WSA_IN` → `capture/vendor_sound.txt`
- [x] vendor clock-дескрипторы → `capture/vendor_clks.txt`:
      va_core 0x30b@19.2M, tx_core 0x30c@19.2M, tx_npl, rx_core 0x30e@22.5792M,
      rx_npl 0x30f@22.5792M, va_npl, wsa_ana — совпадают с Q6AFE_LPASS_CLK_ID_*
- [x] vendor macro props → `capture/vendor_macros.txt`:
      - **va: единственный clock = `lpass_audio_hw_vote` (LPASS_HW_MACRO_VOTE)**
      - rx/tx: только core-clocks, БЕЗ hw голосов (остров уже включён va)
      - va-dmic-rate=600k, tx-dmic-rate=2.4M, va-clk-mux-select=1,
        va-island-mode-muxsel=0x0a7a0000, rx_mclk_mode_muxsel=0x0a5640d8

### Полный путь звука динамика (верифицировано дампом)

```
AP: MultiMedia1 PCM (pcm0p, 48k S24_3LE stereo)
  └─ mixer: "RX_CDC_DMA_RX_1 Audio Mixer MultiMedia1" = 1
AFE backend: RX_CODEC_DMA_RX_1 (q6afe port — codec-DMA внутри wcd937x,
             данные идут по SoundWire напрямую в кодек!)
wcd937x: RX_MACRO RX2 MUX = AIF2_PB
         RX INT2_1 MIX1 INP0 = RX2
         AUX PATH Mode = HP_MODE
         AUX_RDAC Switch = 1
analog:  AUX_OUT → aw87390 (profile=Music) → динамик
         aw87xxx_spk_switch/rcv_switch/profile_switch_0 = Music
```

**Выводы для mainline:**
- Динамику НЕ нужен DAI lpass_rx_macro — поток идёт через
  `RX_CODEC_DMA_RX_1` AFE-порт → codec DMA → внутренние макросы wcd937x.
- Громкость — в DSP (ASM volume): смена media volume НЕ трогает
  ни один mixer-контрол кодека (diff пуст).
- Минимальный набор для динамика: adsp+apr, swr-мастера+lpi+CCs
  (остров + enumeration), wcd937x probe, sound-card со связкой
  MultiMedia1↔RX_CODEC_DMA_RX_1, aw87xxx profile Music.

### Вендорные факты
- machine card: `qcom,bengal-asoc-snd` → mainline-аналог sm8250.c
  (generic qdsp6 sndcard, dai-links из DT)
- num-macros=3, bolero-version=5, wcd937x split-codec=1
- swr: VA-master @0xa740000 (master_id=3, irq 296, 3 порта),
  RX-master @0xa610000 (master_id=2, irq 297, 5 портов) — адреса/irq
  совпадают с нашим dtsi
- micbias1/2/3 = 2.7V, vdd-rxtx/vddpx = 1.8V
- wcd937x RST gpio phandle 0x354 (наш: tlmm gpio92)

### Корень зависания — НАЙДЕН (с высокой вероятностью)

Вендорский `lpi_pinctrl` голосует `lpass_audio_hw_vote`
(= LPASS_HW_MACRO_VOTE, блок 0x3 — питание всего LPASS-острова).
Наш `lpass_tlmm` голосовал `LPASS_HW_DCODEC_VOTE` (блок 0x4 —
внутренний digital codec, которого на khaje НЕТ).

Механика виса:
1. lpi probe OK — регистрация без MMIO-записей в пины.
2. Любой макрос probe → driver core `pinctrl_select_state` →
   MMIO-запись в 0xa7c0000 → регион обесточен (блок 0x3 никогда
   не голосовался) → CPU pipeline висит на load навсегда →
   hard LOCKUP, WQ starvation, i2c таймауты, модем падает.
3. va-only «не висел» было ложным — pinctrl state применяется
   до probe, просто другой тайминг/не дошло до видимости.

Дополнительно: va_macro наделён `macro`=MACRO_VOTE (по образцу
вендора), убраны все DCODEC_VOTE из rx/tx/va.
`pm_runtime_forbid` добавлен макросам (не снимать — runtime_suspend
делает MMIO; без него отложенный suspend может весить снова).

### Vendor probe/boot последовательность (dmesg_full.txt)

```
1.27s  aw87xxx_pa_init v2.0.0.10, chipid 0x76, product=aw87390,
       acf=/vendor/firmware/aw87xxx_acf.bin (1374 B — наш файл ИДЕНТИЧЕН)
2.67s  init: modprobe audio_* (audio_aw87xxx audio_fs1599 audio_bolero_cdc
       audio_va_macro audio_rx_macro audio_tx_macro audio_wcd937x
       audio_wcd937x_slave audio_machine_bengal + ~20 модулей)
4.32s  aw87xxx: acf parsed — 6 профилей, monitor_bin: switch=1 time=1000ms
       vmax table: vbat>70→0x0, 50-70→0xfff46980, 30-50→0xffeca56b,
       0-30→0xffe96a00 (smartPA защита по заряду)
7.17s  bolero-clk-rsc-mngr → va_macro(id3) → rx_macro(id1) →
       tx_macro(id0) → wcd937x_codec + оба slave → swr-wcd ×2
7.24s  bengal-asoc-snd: "found 1 AUX codecs" (aw87xxx = aux dev!)
7.31s  wcd937x_soc_codec_probe; card bengalidpsndcar зарегистрирована
```

- Все макросы/SWR/codec — модули, грузятся init-ом ПОСЛЕ adsp.
- fs15xx@0x34 (foursemi) — receiver PA; модуль загружен, но устройство
  без sysfs-атрибутов (driver не пробился? или просто пустой).
- AUX-path: wcd937x AUX_OUT → aw87390; HS path: HPHL/HPHR на jack
  (fsa4480-i2c-handle в sound node — jack detection chip!).
- MIC BIASes: AMIC1-4 analog + TX/VA DMIC 0-3 → микрофонов до 4 шт.

## Speaker-only план (минимальный стек)

Отброшено: tx_macro, fs15xx, fsa4480, MBHC, capture-link, DMIC.
Оставлено ровно то, что нужно динамику:

```
NPA island vote (q6core, delayed+retry)
→ lpi_tlmm (пины; MMIO только по запросу макросов)
→ lpasscc + lpass_audiocc (клоки шины)
→ rx_macro @a600000 + va_macro @a730000   (tx НЕ нужен —
   va нужен т.к. TX-slave wcd937x сидит на VA-мастере)
→ swr0 @a610000 + swr1 @a740000
→ wcd937x rx+tx slaves (драйвер требует ОБА phandle)
→ sound card: MultiMedia1 ↔ RX_CODEC_DMA_RX_1 (playback only)
→ aw87xxx-mini (уже ок: chipid 0x76, Music применён)
```

Этапы (один билд = один шаг):
0. [ ] NPA vote alone — «LPASS island vote OK» в логе, стабильно
1. [ ] +lpi +CCs — стабильно
2. [ ] +rx_macro +va_macro — RXM/VAM маркеры до конца, без виса
3. [ ] +swr0/swr1 — мастера пробились
4. [ ] +wcd937x slaves — enumeration + codec bound
5. [ ] +sound card — card0 зарегистрирована
6. [ ] aplay → aw87xxx Music → звук

ACM: input глючит на железе (RX байты теряются) — вывод-only,
дамп каждые 30с в порт.

### C. Сравнение
- [ ] Порядок включения: codec→swr→macro→PA vs наш порядок probe
- [ ] Какие clocks/power-domains вендор держит (debugfs clk_summary)
- [ ] Регистры aw87xxx в каждом режиме vs наши таблицы
- [ ] Что реально используется: swr-порты, какие DAI-link'и

## Файлы
- `docs/audio/SPEAKERS.md` — расследование железа
- `docs/audio/android-live.dts` — выгруженный Android DT
- `docs/audio/mixer_paths.xml` — вендорные mixer-пути
- `docs/audio/capture/` — сюда скидывать дампы
