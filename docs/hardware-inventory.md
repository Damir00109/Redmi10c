# Инвентарь железа Redmi 10C (fog/rain)

Снято с живого устройства из Android (слот A, root/Magisk):
getprop, dumpsys sensorservice/media.camera, sysfs.
Модель: **220333QNY** (rain, NFC-вариант), `ro.board.platform=bengal`,
DT-модель: `RAIN KHAJE IDP nopmi`.

## Полная таблица распаянных компонентов

| # | Компонент | Чип / партнамбер | Шина / адрес | Вендор |
|---|---|---|---|---|
| 1 | SoC | **SM6225 «Khaje»** (CPU 8× Kryo 265, GPU **Adreno 610 v2**, модем MPSS.HA, CDSP, ADSP — всё в кристалле) | — | Qualcomm |
| 2 | PMIC | **PM6125** (зарядка-часть, VADC, LDO вибратора, термозоны) | SPMI 1c40000 | Qualcomm |
| 3 | DRAM | ~3 ГБ LPDDR4x (over SoC, PoP/по плате) | — | — |
| 4 | Основная память | **SDINDDK4-64G** UFS 2.x, 64 ГБ | UFS 4804000/4807000 | SanDisk/WDC |
| 5 | Панель | `c3q_43_03_0b` FHD+ video-mode | DSI 5e00000 | CSOT/Tianma-class |
| 6 | Тачскрин | **FT8006S** (`fts_ts`; модалиас `NVT-ts-spi` фантомен) | SPI0 (4a80000) | Focaltech |
| 7 | Подсветка | `fs15xx` | i2c0-0034 | FocalTech-class |
| 8 | Подсветка (доп.) | `bkl-bl` | i2c0-0036 | — |
| 9 | LCD bias | `lcm-bias` | i2c0-003e | — |
| 10 | Камера задняя | **S5KJN1** 50МП (модуль Sunny; альтернативы HI1337/SC500CS) | CCI0 + CSIPHY | Samsung |
| 11 | Камера фронт | **HI556** 5МП (модуль AAC «V6457») | CCI1, i2c1-0x28 | Himax |
| 12 | Камера depth | **OV02B1B** 2МП (модуль AAC «B313») | CCI | OmniVision |
| 13 | VCM (AF) | **GT9764** | в модуле задней камеры | GIANTEC |
| 14 | EEPROM камер | at24: 24c128 + 24c64 + 24c02 | в модулях камер | Atmel-compat |
| 15 | Питание камер | **WL2866D** LDO-PMIC | i2c0-0028 | WillSemi |
| 16 | Аудиокодек | **WCD937x** (WCD9375-класс, 2 SWR-slave) | SoundWire swr0/1 | Qualcomm |
| 17 | Усилитель динамика | **AW87xxx** PA | i2c0-0058 | Awinic |
| 18 | Акселерометр | **SC7A20** | sensors/SSC (i2c) | Silan |
| 19 | ALS + Proximity | **TMD2755** | sensors/SSC (i2c) | ams AG |
| 20 | SAR-датчик | **ADUX1050** | sensors/SSC | Analog Devices |
| 21 | Сканер отпечатка | FPC rear (`ro.boot.fpsensor=fpc`, `fp-keys`) | SPI/GPIO | Fingerprint Cards |
| 22 | Wi-Fi + BT | **WCN3990** (ath10k / hci_qca UART) | SNOC + GENI UART | Qualcomm |
| 23 | NFC | **pn8x** (`nq-nci`, PN80T/SN100-класс) | i2c1-002a | NXP |
| 24 | Зарядник | **SMB1351** | i2c1-0055 | Summit |
| 25 | Fuel gauge | **SH366101** | i2c0-0055 | Sinowealth |
| 26 | USB-C CC №1 | **FUSB303** | i2c1-0021 | onsemi/Fairchild |
| 27 | USB-C CC №2 | **WUSB3801** | i2c1-0060 | WillSemi |
| 28 | Вибромотор | coin-motor через **qpnp-vibrator-ldo** (LDO в PM6125) | GPIO+LDO | — |
| 29 | Аудиоразъём-детект | headset/button jack (в WCD937x MBHC) | SoundWire MBHC | Qualcomm |
| 30 | SD-слот | host-контроллер SDHCI в SoC (карта SD16G опознана) | mmc/sdhci | — |
| 31 | Детект антенн | `ant_check`/`ant_div_check` input-линии | GPIO | — |

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

| Устройство | Адрес/имя | Функция |
|---|---|---|
| APSS CPU | `f521000.cpufreq`, `pmu`, `psci` | 8× Kryo 265 (2 кластера), cpufreq-драйвер |
| GPU | `5900000.gpu` + `596a000.gmu` | Adreno 610 v2 + GMU (SW/ZAP-firmware) |
| DSP модема | `6080000.remoteproc` | MPSS — модем/Location/QMI |
| DSP аудио | `ab00000.remoteproc` | ADSP — LPASS, APR-сервисы (q6afe/adm/core), fastrpc CB ×7 |
| CDSP | (в составе mapss/cdsp tz) | compute-DSP, HVX — на fog сервисы видны в thermal, отдельного remoteproc-ноды в DT нет |
| Wi-Fi MAC | `c800000.wifi` | WCN3990 SNOC (ath10k) |
| IPA | `5840000.ipa` | IP-акселератор модем↔AP (rmnet_ipa0, QMAP v4.2) |
| UFS HC | `4804000.ufshc` | UFS-контроллер, 6 LUN |
| UFS PHY | `4807000.phy` | QMP v4 PHY |
| SD host | `4784000.mmc` | SDHCI, microSD |
| USB3 | `4e0000.usb` | DWC3 host/gadget (DMA 32-бит клэмп — quirk) |
| USB PHY | `1613000.phy` | SNPS femto v2 |
| Display MDP | `5e01000.display-controller` | композитор/видео-пути |
| DSI host | `5e94000.dsi` + `5e94400.phy` | DSI-хост + 7nm PHY |
| CAMSS | `5c11000.camss` | CSIPHY×3/CSID/VFE — камеры |
| CCI | `5c1b000.cci` | camera i2c ×2 (i2c-0/1: s5kjn1, hi556, gt9764, eeprom'ы, flash) |
| GENI QUP | `4ac0000.geniqup` | обёртка SE-шин: i2c `4a84000`/`4a88000`, SPI0 (тач), UART `4a8c000` (BT) |
| SPMI arbiter | `1c40000.spmi` | шина к PMIC |
| TLMM GPIO | `400000.pinctrl` | основной GPIO/irq (reserved_gpios/irq_quiesce quirks) |
| LPASS pinctrl | `a7c0000.pinctrl` | GPIO в аудио-домене |
| SoundWire | `a610000`/`a740000` + codec `a600000`/`a730000` | 2 мастера → WCD937x |
| Interconnect | `1880000`, `1900000`, `4480000` + `icc_smd_rpm` | NoC: aggre/mmnoc/cnoc+snoc (usb_axi keep-alive) |
| Clock controllers | GCC `1400000`, GPUCC `5990000`, DISPCC `5f0000`, LPASS-CC `a7ec000`/`a6a9000` | деревья клоков |
| SMMU/IOMMU | `59a0000`, `c600000` | трансляция DMA (GPU, DSP, camss) |
| tsens | `4411000.thermal-sensor` | температурные сенсоры кристалла |
| RPM/SMD | `icc_smd_rpm`, `remoteproc:glink-edge:rpm-requests` | голоса клоков/регуляторов/PD через RPM |
| Прочее | `340000.hwlock`, `smem`, `smp2p-*`, `f111000.mailbox`, `f120000.timer`, `45f0000.sram`, `firmware:scm`, `qcom_qseecom`, `qcom-socinfo`, `9ff00000.ramoops`, `89b01000.memory` | hw-локи, shared mem, IPC, TZ-интерфейсы, reserved-mem |

### Блоки PMIC (PM6125, SPMI)

| Устройство | Адрес | Функция |
|---|---|---|
| PON | `pmic@0:pon@800` | power-key, resin, soft-reset |
| VADC | `pmic@0:adc@3100` | термодатчики, батарея, camera-flash therm |
| RTC | `pmic@0:rtc@6000` | rtc-pm8xxx (читает; запись — PMIC write-protect, как на стоке) |
| Temp-alarm | `pmic@0:temp-alarm@2400` | thermal-шина pm6125-tz |
| PMIC GPIO | `pmic@0:gpio@c000` | gpio-ключи/LED на PMIC |
| Vibrator LDO | `qpnp-vibrator-ldo` | питание вибромотора |

### Периферия платы

| Устройство | Интерфейс | Что это |
|---|---|---|
| SIM ×2 | через модем/UIM | слот 1 рабочий; слот 2 = регистрация без data (задокументировано) |
| microSD | `4784000.mmc` | hot-detect через polling (gpio88 reserved) |
| USB-C порт | DWC3 + FUSB303 + WUSB3801 | данные + PD/CC-контроллеры |
| Батарея | bms (SMB1351+SH366101) | ~5000 мАч |
| Динамик | AW87xxx PA → driver | громкоговоритель |
| Ресивер | WCD937x | телефонный динамик |
| Микрофоны ×2 | аналог → WCD937x ADC | основной + шумодав |
| Джек 3.5мм | WCD937x MBHC + FSA4480 | детект гарнитуры/кнопок |
| Кнопки | pmic@0 pon (pwrkey/resin) + gpio-keys | power, vol± |
| Fingerprint | FPC rear, `fp-keys` | SPI+GPIO |
| Вибромотор | qpnp-vibrator-ldo + gpio | coin-motor |
| Вспышка | `camera-flash@0` (CCI0) | LED torch/flash |
| Антенны | main + diversity + `ant_check`/`ant_div_check` | LTE/Wi-Fi антенны + GPIO-детект |
| NFC-антенна | pn8x | рамка NFC |

## Платформа

| Блок | Чип / факты |
|---|---|
| SoC | Qualcomm **SM6225 «Khaje»** (семейство bengal; родственник SM6115/SM6375) |
| CPU | 8× Kryo 265: кластер до 1.9 ГГц (cpu0) + производительный кластер |
| GPU | **Adreno 610 v2** (`kgsl gpu_model = Adreno610v2`) |
| RAM | ~3 ГБ доступно (`MemTotal 2726 МБ` — за вычетом carveout'ов) |
| Flash | **SanDisk iNAND SDINDDK4-64G** UFS 2.x 64 ГБ (vendor WDC, `/sys/block/sda`) |
| SD | слот microSD (SD16G вставлена — видна как mmcblk0) |
| Модем | интегрированный MPSS.HA.1.1 (внешнего SDX нет) |
| PMIC | **PM6125** (SPMI: vadc, bms-часть, qpnp-vibrator-ldo) |

## Дисплей и тач

| Блок | Чип / факты |
|---|---|
| Панель | `dsi_panel_c3q_43_03_0b_fhdp_video_display` — FHD+ video-mode DSI |
| Тач | **Focaltech fts_ts** (FT8006S) на SPI0 — NB: Android modalias говорит `NVT-ts-spi`, но реально пробивается драйвер `fts_ts` (input name). Совпадает с mainline |
| Подсветка | `fs15xx` + `bkl-bl` (i2c0-0034/0036), LCD bias: `lcm-bias` (i2c0-003e) |

## Камеры (config `c3q`)

| Роль | Сенсор | Модуль | Примечание |
|---|---|---|---|
| Задняя | **Samsung S5KJN1** 50MP | Sunny (на этом юните); альтернативы AAC/OFilm, HI1337, SC500CS в vendor-паке | полный CAMSS-стрим подтверждён |
| Фронт | **Himax HI556** 5MP | AAC «V6457» | CCI1, i2c 0x28 |
| Depth | **OmniVision OV02B1B** 2MP | AAC «B313» | |
| AF/VCM | GT9764 | в модуле задней камеры | DAC-калибровка в EEPROM |
| EEPROM | at24 ×3 (24c128/24c64/24c02) | в модулях | module info + AWB/LSC |
| Питание камер | **WL2866D** LDO-PMIC | i2c0-0028 | DVDD/AVDD |
| Вспышка | camera-flash через CCI0 + `soc:flashlight` | | |

## Аудио

| Блок | Чип |
|---|---|
| Codec | **WCD937x ×2** (SoundWire slaves a01170223/24 — WCD9375-семейство) |
| PA | **Awinic AW87xxx** (i2c0-0058) |
| Карта | `bengal-idp-snd-card` |

## Сенсоры (dumpsys sensorservice, 22 шт.)

| Чип | Тип | Вендор |
|---|---|---|
| **SC7A20** | акселерометр | Silan (SLCompany) |
| **TMD2755** | ALS + proximity | ams AG |
| **ADUX1050** | SAR-датчик (ёмкостный, RF) | Analog Devices |
| — | виртуальные (step/tilt/orientation/SMD/stationary/motion) | Qualcomm SSC |
| pickup | virtual | Xiaomi |

**Гироскопа и магнитометра НЕТ** — аппаратно отсутствуют (фьюжн-строки
в dumpsys ссылаются на accel-derived виртуальные).

**Fingerprint:** FPC, задний (`ro.boot.fpsensor=fpc`, input `fp-keys`).

## Питание и батарея

| Блок | Чип |
|---|---|
| Зарядник | **SMB1351** (i2c1-0055) |
| Fuel gauge | **SH366101** (i2c0-0055, Sinowealth) |
| USB-C CC | **FUSB303** (i2c1-0021) + **WUSB3801** (i2c1-0060) |
| Вибромотор | `qcom,qpnp-vibrator-ldo` — LDO PMIC (GPIO-vibrator) |
| Батарея | ~5000 мАч (bms charge_full 41000 в единицах PMIC) |

## Связь

| Блок | Чип |
|---|---|
| Wi-Fi/BT | **WCN3990** (cnss2 5.2.022.12B, BT по UART — совпадает с mainline ath10k/hci_qca) |
| NFC | **NXP pn8x** (`nq-nci` на i2c1-002a, `ro.hardware.nfc_nci=pn8x`) |
| GPS | в составе модема (PDS/Location) |
| IR | отсутствует |

## Термозоны (выборочно)

pm6125-tz, cpu-1-x, cpuss-x, gpu(-usr/-step), mdm-0/1, cdsp-hvx,
wlan-usr, camera-usr, display-usr, pa-therm0, rf-pa1, emmc-ufs-therm,
xo-therm, bms, battery — 40+ зон, контроллеры PMIC+VADC+CPU-сенсоры.

## Расхождения с mainline-деревом

- SPI-modalalias `NVT-ts-spi` — фантом; реальный чип Focaltech (input `fts_ts`).
- i2c-карта Android шире: fs15xx/bkl-bl/lcm-bias (подсветка/bias),
  sh366101 fuel gauge, aw87xxx PA, fusb303/wusb3801 (USB-C) —
  на mainline часть нод отсутствует/не пробивается (см. fog.dts).
- `nq-nci` (NFC) на mainline живёт как `wl2866d`-сосед — chip id 0x64.
