# Инвентарь железа Redmi 10C (fog/rain)

Снято с живого устройства из Android (слот A, root/Magisk):
getprop, dumpsys sensorservice/media.camera, sysfs.
Модель: **220333QNY** (rain, NFC-вариант), `ro.board.platform=bengal`,
DT-модель: `RAIN KHAJE IDP nopmi`.

Статусы mainline: **FULL** — работает полностью;
**NORMAL** — работает с оговорками/quirks; **LOW** — частично или не
заведено в DT; **NOT SUPPORTED** — нет поддержки.

## Полная таблица распаянных компонентов

| # | Компонент | Чип / партнамбер | Шина / адрес | Вендор | Mainline |
|---|---|---|---|---|---|
| 1 | SoC | **SM6225 «Khaje»** (CPU 8× Kryo 265, GPU **Adreno 610 v2**, модем MPSS.HA, ADSP/CDSP) | — | Qualcomm | **NORMAL** (большинство блоков full; CDSP не заведён) |
| 2 | PMIC | **PM6125** (зарядка-часть, VADC, LDO вибратора, термозоны) | SPMI 1c40000 | Qualcomm | **NORMAL** (rtc read-only — как на стоке) |
| 3 | DRAM | ~3 ГБ LPDDR4x | — | — | **FULL** |
| 4 | Основная память | **SDINDDK4-64G** UFS 2.x, 64 ГБ | UFS 4804000/4807000 | SanDisk/WDC | **FULL** (quirk: `disable-runtime-pm` + ahit=0) |
| 5 | Панель | `c3q_43_03_0b` FHD+ video-mode | DSI 5e00000 | CSOT/Tianma-class | **FULL** |
| 6 | Тачскрин | **FT8006S** (`fts_ts`; модалиас `NVT-ts-spi` фантомен) | SPI0 (4a80000) | Focaltech | **FULL** |
| 7 | Подсветка | `fs15xx` | i2c2-0034 | FocalTech-class | **FULL** (пробивается, регулирует яркость) |
| 8 | Подсветка (доп.) | `bkl-bl` | i2c0-0036 | — | **NOT SUPPORTED** (альтернативная реализация; функцию берёт fs15xx) |
| 9 | LCD bias | `lcm-bias` | i2c0-003e | — | **NOT SUPPORTED** (не пробивается; питание панели идёт без отдельного bias) |
| 10 | Камера задняя | **S5KJN1** 50МП (модуль Sunny; альтернативы HI1337/SC500CS) | CCI0 + CSIPHY | Samsung | **NORMAL** (стрим RAW10 работает; нет AE/AWB — ISP-уровень) |
| 11 | Камера фронт | **HI556** 5МП (модуль AAC «V6457») | CCI1, i2c1-0x28 | Himax | **NORMAL** (кадры идут; нет AE/AWB) |
| 12 | Камера depth | **OV02B1B** 2МП (модуль AAC «B313») | CCI | OmniVision | **LOW** (в DT есть, в i2c-карте не виден — не проверен стрим) |
| 13 | VCM (AF) | **GT9764** | i2c0-000c, в модуле задней камеры | GIANTEC | **NORMAL** (пробивается; автофокус внутри CAMSS-пути) |
| 14 | EEPROM камер | at24: 24c128 + 24c64 + 24c02 | i2c0-51/i2c1-51 | Atmel-compat | **FULL** (читаются, содержимое верифицировано) |
| 15 | Питание камер | **WL2866D** LDO-PMIC | i2c2-0028 | WillSemi | **FULL** |
| 16 | Аудиокодек | **WCD937x** (WCD9375-класс, 2 SWR-slave) | SoundWire swr0/1 | Qualcomm | **FULL** (динамик + гарнитура) |
| 17 | Усилитель динамика | **AW87xxx** PA | i2c2-0058 | Awinic | **FULL** |
| 18 | Акселерометр | **SC7A20** | SSC (SLPI) | Silan | **NORMAL** (данные идут через SSC/hexagonrpcd; iio-sensor-proxy в работе) |
| 19 | ALS + Proximity | **TMD2755** | SSC (SLPI) | ams AG | **NORMAL** (как accel) |
| 20 | SAR-датчик | **ADUX1050** | SSC | Analog Devices | **LOW** (драйвера/ноды в mainline-дереве нет) |
| 21 | Сканер отпечатка | FPC rear (`ro.boot.fpsensor=fpc`, `fp-keys`) | SPI/GPIO | Fingerprint Cards | **NOT SUPPORTED** |
| 22 | Wi-Fi | **WCN3990** (ath10k SNOC) | c800000.wifi | Qualcomm | **FULL** (~1.1 Гбит AP-режим проверен) |
| 22a | Bluetooth | тот же WCN3990 (hci_qca) | GENI UART 4a8c000 | Qualcomm | **NORMAL** (A2DP работает; редкий wedge при re-init — известный дебт) |
| 23 | NFC | **pn8x** (`nq-nci`, PN80T/SN100-класс) | i2c1-002a | NXP | **LOW** (чип пробивается chip id 0x64; стека/использования нет) |
| 24 | Зарядник | **SMB1351** | i2c3-0055 | Summit | **FULL** |
| 25 | Fuel gauge | **SH366101** | i2c2-0055 | Sinowealth | **FULL** (SOC/V/I/Charging видны) |
| 26 | USB-C CC №1 | **FUSB303** | i2c1-0021 | onsemi/Fairchild | **NOT SUPPORTED** (в mainline-дереве не пробивается) |
| 27 | USB-C CC №2 | **WUSB3801** | i2c3-0060 | WillSemi | **NORMAL** (пробивается; роль/PD минимальны) |
| 28 | Вибромотор | coin-motor через **qpnp-vibrator-ldo** | GPIO+LDO PM6125 | — | **FULL** |
| 29 | Джек-детект | WCD937x MBHC + **FSA4480** | SoundWire + i2c | Qualcomm/onsemi | **FULL** (гарнитура + кнопки) |
| 30 | SD-слот | SDHCI (`4784000.mmc`) | mmc | — | **FULL** (детект polling — gpio88 reserved) |
| 31 | Детект антенн | `ant_check`/`ant_div_check` | GPIO | — | **LOW** (GPIO-линии не заведены; не нужны для работы) |

**Отсутствует (проверено):** гироскоп, магнитометр/компас, IR-blaster,
внешний модем (интегрирован в SoC), датчик Холла, LED-индикатор.

**Не перечисляется из userspace** (нужна схема/разборка): RF front-end
модема (PA/антенные тюнеры на RFFE), MEMS-микрофоны ×2, партнамбер
драйвера вспышки (`qcom,camera-flash@0` на CCI0), маркировка DRAM и
точный вендор панели (только код `c3q`), динамик/ресивер.

## Полная карта устройств и подсистем

Всё, что реально живёт на плате: блоки SoC (MMIO-адрес из DT), шины,
периферия. Собрано из `/sys/bus/platform/devices` + DT на kernel #9.

### Блоки SoC (SM6225 Khaje)

| Устройство | Адрес/имя | Функция | Mainline |
|---|---|---|---|
| APSS CPU | `f521000.cpufreq`, `pmu`, `psci` | 8× Kryo 265 (2 кластера), cpufreq | **FULL** |
| GPU | `5900000.gpu` + `596a000.gmu` | Adreno 610 v2 + GMU (SW/ZAP) | **FULL** (SMMU/devfreq/GMU-pwr) |
| DSP модема | `6080000.remoteproc` | MPSS — модем/Location/QMI | **NORMAL** (QMI/MM/LTE-data; voice/SMS частично; слот2 без data) |
| DSP аудио | `ab00000.remoteproc` | ADSP — LPASS/APR (q6afe/adm/core), fastrpc CB ×7 | **NORMAL** (звук/сенсоры работают; редкий `AFE vote` шум при старте) |
| CDSP | thermal-зоны только | compute-DSP, HVX | **LOW** (отдельной remoteproc-ноды в DT нет) |
| Wi-Fi MAC | `c800000.wifi` | WCN3990 SNOC (ath10k) | **FULL** |
| IPA | `5840000.ipa` | IP-акселератор модем↔AP (rmnet_ipa0, QMAP v4.2) | **NORMAL** (работает; PAS-sequencing — кастомный патч) |
| UFS HC | `4804000.ufshc` | UFS-контроллер, 6 LUN | **FULL** (khaje quirks + ref-clk) |
| UFS PHY | `4807000.phy` | QMP v4 PHY | **FULL** |
| SD host | `4784000.mmc` | SDHCI, microSD | **FULL** |
| USB3 | `4e0000.usb` | DWC3 host/gadget | **FULL** (quirk: DMA 32-бит клэмп) |
| USB PHY | `1613000.phy` | SNPS femto v2 | **FULL** |
| Display MDP | `5e01000.display-controller` | композитор/видео-пути | **FULL** |
| DSI host | `5e94000.dsi` + `5e94400.phy` | DSI-хост + 7nm PHY | **FULL** (PLL-err логирование восстановлено) |
| CAMSS | `5c11000.camss` | CSIPHY×3/CSID/VFE — камеры | **NORMAL** (стримы есть; нет AE/AWB; `SET_RATE_PARENT` снят — обяз. quirk) |
| CCI | `5c1b000.cci` | camera i2c ×2 | **FULL** |
| GENI QUP | `4ac0000.geniqup` | i2c `4a84000`/`4a88000`, SPI0 (тач), UART `4a8c000` (BT) | **FULL** |
| SPMI arbiter | `1c40000.spmi` | шина к PMIC | **FULL** |
| TLMM GPIO | `400000.pinctrl` | основной GPIO/irq | **FULL** (quirks: reserved_gpios, irq_quiesce) |
| LPASS pinctrl | `a7c0000.pinctrl` | GPIO в аудио-домене | **FULL** |
| SoundWire | `a610000`/`a740000` + codec `a600000`/`a730000` | 2 мастера → WCD937x | **FULL** |
| Interconnect | `1880000`, `1900000`, `4480000` + `icc_smd_rpm` | NoC: aggre/mm/cnoc+snoc | **NORMAL** (обязателен usb_axi keep-alive на snoc+cnoc — root-caused) |
| Clock controllers | GCC `1400000`, GPUCC `5990000`, DISPCC `5f0000`, LPASS-CC `a7ec000`/`a6a9000` | деревья клоков | **NORMAL** (gpucc silicon-fix применён) |
| SMMU/IOMMU | `59a0000`, `c600000` | трансляция DMA | **NORMAL** |
| tsens | `4411000.thermal-sensor` | температурные сенсоры | **FULL** |
| RPM/SMD | `icc_smd_rpm`, `rpm-requests` | голоса клоков/регуляторов/PD | **NORMAL** |
| TЗ/qseecom | `firmware:scm`, `qcom_qseecom`, `qcom-socinfo` | efivars read-only, трастлет | **NORMAL** (запись новых efivar отбрасывается — как сток; `uefi-rtc-info` не включать) |
| Прочее | `340000.hwlock`, `smem`, `smp2p-*`, `f111000.mailbox`, `f120000.timer`, `45f0000.sram`, `9ff00000.ramoops`, `89b01000.memory` | hw-локи, shared mem, IPC, reserved-mem | **FULL** |

### Блоки PMIC (PM6125, SPMI)

| Устройство | Адрес | Функция | Mainline |
|---|---|---|---|
| PON | `pmic@0:pon@800` | power-key, resin, soft-reset | **FULL** |
| VADC | `pmic@0:adc@3100` | термодатчики, батарея, camera-flash therm | **FULL** |
| RTC | `pmic@0:rtc@6000` | rtc-pm8xxx | **LOW** (только чтение — PMIC write-protect, на стоке так же) |
| Temp-alarm | `pmic@0:temp-alarm@2400` | pm6125-tz | **FULL** |
| PMIC GPIO | `pmic@0:gpio@c000` | gpio-ключи/LED на PMIC | **NORMAL** |
| Vibrator LDO | `qpnp-vibrator-ldo` | питание вибромотора | **FULL** |

### Периферия платы

| Устройство | Интерфейс | Что это | Mainline |
|---|---|---|---|
| SIM slot 1 | модем/UIM | voice/SMS/data | **NORMAL** (data работает после выставления подписки Android'ом) |
| SIM slot 2 | модем/UIM | регистрация есть | **LOW** (data отклоняется модемом `no-service`; гасится скриптом) |
| microSD | `4784000.mmc` | карточка читается/пишется | **FULL** |
| USB-C порт | DWC3 + CC-контроллеры | данные + зарядка | **NORMAL** (data/charge OK; роль-менеджмент CC частично) |
| Батарея | SMB1351 + SH366101 | ~5000 мАч | **FULL** |
| Динамик | AW87xxx PA | громкоговоритель | **FULL** |
| Ресивер | WCD937x | телефонный динамик | **NORMAL** (путь есть; voice-профили не проверены) |
| Микрофоны ×2 | аналог → WCD937x ADC | основной + шумодав | **NORMAL** (захват есть; routing/UCM минимальны) |
| Джек 3.5мм | WCD937x MBHC + FSA4480 | детект + кнопки гарнитуры | **FULL** |
| Кнопки | pon (pwrkey/resin) + gpio-keys | power, vol± | **FULL** |
| Fingerprint | FPC rear | биометрия | **NOT SUPPORTED** |
| Вибромотор | qpnp-vibrator-ldo | coin-motor | **FULL** |
| Вспышка | `camera-flash@0` (CCI0) | LED torch/flash | **NORMAL** (torch есть; интеграция с камерой не проверена) |
| Антенны | main + diversity + check-GPIO | LTE/Wi-Fi приём | **NORMAL** (основной тракт работает; diversity-check GPIO не заведены) |
| NFC-антенна | pn8x | рамка | **LOW** (чип id читается; стека нет) |

## Сводка поддержки (mainline kernel #9)

- **FULL**: панель, тач, UFS, SD, DRAM, GPU, display-стек, Wi-Fi,
  звук (динамик+гарнитура+джек), зарядка, fuel gauge, вибромотор,
  кнопки, подсветка, WL2866D/EEPROM камер, базовые SoC-блоки
  (pinctrl, SPMI, GENI, SoundWire, tsens, CCI, USB).
- **NORMAL**: SoC в целом, PMIC, камеры (нет AE/AWB), модем/LTE
  (data есть, voice/SMS частично), Bluetooth (редкий wedge),
  ADSP/LPASS (стартовый AFE-шум), IPA, CAMSS, interconnect/clk-
  quirks, RTC (read-only), USB-C роли, SIM-1, микрофоны/ресивер,
  вспышка.
- **LOW**: depth-камера (не проверена), SAR ADUX1050, NFC (только
  id), SIM-2, ant-check GPIO, CDSP-нода.
- **NOT SUPPORTED**: сканер отпечатков (FPC), FUSB303, bkl-bl,
  lcm-bias.
