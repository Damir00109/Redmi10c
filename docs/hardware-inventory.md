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
| 7 | Аудио smart-boost | `fs15xx` (foursemi FS1599) | i2c2-0034 | Foursemi | **NOT SUPPORTED** (нода есть, драйвера в дереве нет; boost PVDD для AW87xxx — работает без него) |
| 8 | Подсветка (доп.) | `bkl-bl` | i2c0-0036 | — | **NOT SUPPORTED** (не пробивается; подсветка идёт через DSI-панель `5e94000.dsi.0`) |
| 9 | LCD bias | `lcm-bias` | i2c0-003e | — | **NOT SUPPORTED** (не пробивается; питание панели идёт без отдельного bias) |
| 10 | Камера задняя | **S5KJN1** 50МП (модуль Sunny; альтернативы HI1337/SC500CS) | CCI0 + CSIPHY | Samsung | **NORMAL** (стрим RAW10 работает; нет AE/AWB — ISP-уровень) |
| 11 | Камера фронт | **HI556** 5МП (модуль AAC «V6457») | CCI1, i2c1-0x28 | Himax | **NORMAL** (кадры идут; нет AE/AWB) |
| 12 | Камера depth | **OV02B1B** 2МП (модуль AAC «B313») | CCI0 master 1, CSIPHY1 | OmniVision | **LOW** — была `status = disabled` без порта; **включена и связана** (2026-10-18): драйвер `1-0059: ov02b1b` пробивается, в media-графе есть сущность и линк `msm_csiphy1 ↔ ov02b1b [ENABLED]`, формат Y10_1X10 1600x1200. Поток пока не идёт (`STREAMON → EPIPE`) — нужна отдельная работа с CAMSS-пайплайном (CSIPHY/CSID/VFE) |
| 13 | VCM (AF) | **GT9764** | i2c0-000c, в модуле задней камеры | GIANTEC | **NORMAL** (пробивается; автофокус внутри CAMSS-пути) |
| 14 | EEPROM камер | at24: 24c128 + 24c64 + 24c02 | i2c0-51/i2c1-51 | Atmel-compat | **FULL** (читаются, содержимое верифицировано) |
| 15 | Питание камер | **WL2866D** LDO-PMIC | i2c2-0028 | WillSemi | **FULL** |
| 16 | Аудиокодек | **WCD937x** (WCD9375-класс, 2 SWR-slave) | SoundWire swr0/1 | Qualcomm | **NORMAL** (карта/PCM/1135 миксеров на месте, BT-аудио работает; локальный маршрут (динамик) в PulseAudio не поднят — только `auto_null`) |
| 17 | Усилитель динамика | **AW87xxx** PA | i2c2-0058 | Awinic | **FULL** |
| 18 | Акселерометр | **SC7A20** | SSC (SLPI) | Silan | **FULL** — ядерных IIO нет (шина DSP), данные идут через `hexagonrpcd`/libssc и публикуются в приложения мостом `rain-sensor-bridge` (`net.hadess.SensorProxy`) |
| 19 | ALS + Proximity | **TMD2755** | SSC (SLPI) | ams AG | **FULL** (ALS и proximity — через мост `rain-sensor-bridge`: `LightLevel`, `ProximityNear`) |
| 20 | SAR-датчик | **ADUX1050** | SSC | Analog Devices | **LOW** (драйвера/ноды в mainline-дереве нет) |
| 21 | Сканер отпечатка | FPC rear (`ro.boot.fpsensor=fpc`, `fp-keys`) | SPI/GPIO | Fingerprint Cards | **NOT SUPPORTED** |
| 22 | Wi-Fi | **WCN3990** (ath10k SNOC) | c800000.wifi | Qualcomm | **FULL** (~1.1 Гбит AP-режим проверен) |
| 22a | Bluetooth | тот же WCN3990 (hci_qca) | GENI UART 4a8c000 | Qualcomm | **NORMAL** (A2DP работает; редкий wedge при re-init — известный дебт) |
| 23 | NFC | **pn8x** (`nq-nci`, PN80T/SN100-класс) | i2c1-002a | NXP | **LOW** (чип пробивается chip id 0x64; стека/использования нет) |
| 24 | Зарядник | **SMB1351** | i2c3-0055 | Summit | **FULL** |
| 25 | Fuel gauge | **SH366101** | i2c2-0055 | Sinowealth | **FULL** (SBS: SOC/V/I/temp/циклы/STATUS/HEALTH + gaugeinfo-блоки: Qmax→SOH, энергия мВт·ч, OCV, внутр. темпа; требует FIFO на i2c — см. quirks) |
| 26 | USB-C CC №1 | **FUSB303** | i2c1-0021 | onsemi/Fairchild | **NOT SUPPORTED** — сырой probe по всем 4 AP-шинам (0x21/0x22/0x25) даёт NACK (`ENXIO`); в отличие от WUSB3801/smb1351, которые дают `EBUSY` как занятые драйверами. Драйверу не к чему биндиться |
| 27 | USB-C CC №2 | **WUSB3801** | i2c3-0060 | WillSemi | **NORMAL** (пробивается; роль/PD минимальны) |
| 28 | Вибромотор | coin-motor через **qpnp-vibrator-ldo** | GPIO+LDO PM6125 | — | **FULL** |
| 29 | Джек-детект | WCD937x MBHC + **FSA4480** | SoundWire + i2c | Qualcomm/onsemi | **FULL** (гарнитура + кнопки) |
| 30 | SD-слот | SDHCI (`4784000.mmc`) | mmc | — | **FULL** (детект polling — gpio88 reserved) |
| 31 | Детект антенн | `ant_check`/`ant_div_check` | GPIO | — | **NOT SUPPORTED** — номера линий достали из стокового DT (`soc/ant_check`: TLMM **107**, `soc/ant_check_div`: TLMM **25**), но завести их у нас нельзя: GPIO 25 в нашем `reserved_gpios` (PM8008 IRQ/EN — MMIO-доступ вешает SoC), а GPIO 107 в SOUTH-банке, который тоже может вешать (см. комментарий про gpio101 в fog.dts). На Android это вендорские input-устройства `ant_check-input`/`ant_div_check-input` |

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
| DSP аудио | `ab00000.remoteproc` | ADSP — LPASS/APR (q6afe/adm/core), fastrpc CB ×7 | **NORMAL** — ADSP поднимается ядром (`auto_boot=true`), PD-маппер in-kernel (`QCOM_PD_MAPPER`); **resume починен**: падал из-за fastrpc-сессии `hexagonrpcd` — гасится до сна (`rain-sensor-suspend`) |
| CDSP | thermal-зоны только | compute-DSP, HVX | **LOW** (отдельной remoteproc-ноды в DT нет) |
| Wi-Fi MAC | `c800000.wifi` | WCN3990 SNOC (ath10k) | **FULL** |
| IPA | `5840000.ipa` | IP-акселератор модем↔AP (rmnet_ipa0, QMAP v4.2) | **NORMAL** (работает; PAS-sequencing — кастомный патч) |
| UFS HC | `4804000.ufshc` | UFS-контроллер, 6 LUN | **FULL** (khaje quirks + ref-clk) |
| UFS PHY | `4807000.phy` | QMP v4 PHY | **FULL** |
| SD host | `4784000.mmc` | SDHCI, microSD | **FULL** |
| USB3 | `4e0000.usb` | DWC3 host/gadget | **FULL** — QSCRATCH/клоки/ресет теперь во владении ядра (нода-обёртка `usb@4ef8800` + `qcom,select-utmi-as-pipe-clk`); из userspace MMIO убран |
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
| PMIC GPIO | `pmic@0:gpio@c000` | gpio-ключи/LED на PMIC | **FULL** (9 линий, состояния читаются; 3 не используются — аудит 2026-10-18) |
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
| Ресивер | WCD937x | телефонный динамик | **NORMAL** — DAPM-граф кодека содержит `EAR/EAR_RDAC/EAR PGA` (путь есть); звуковой тест не делался |
| Микрофоны ×2 | аналог → WCD937x ADC | основной + шумодав | **NORMAL** (захват есть; routing/UCM минимальны) |
| Джек 3.5мм | WCD937x MBHC + FSA4480 | детект + кнопки гарнитуры | **FULL** |
| Кнопки | pon (pwrkey/resin) + gpio-keys | power, vol± | **FULL** |
| Fingerprint | FPC rear | биометрия | **NOT SUPPORTED** |
| Вибромотор | qpnp-vibrator-ldo | coin-motor | **FULL** |
| Вспышка | `camera-flash@0` (CCI0) | LED torch/flash | **NORMAL** — torch проверен (`white:torch`, `max_brightness=1`, вкл/выкл); v4l2-flash API не заведён (интеграция с камерой) |
| Антенны | main + diversity + check-GPIO | LTE/Wi-Fi приём | **NORMAL** (основной тракт работает; diversity-check GPIO не заведены) |
| NFC-антенна | pn8x | рамка | **LOW** (чип id читается; стека нет) |

## Проверка на живом устройстве (2026-10-18, Ubuntu слот B)

Повторная проверка после работ по переносу драйверов в ядро и по
suspend/resume. Изменилось с прошлого прогона: fuel gauge отдаёт полную
телеметрию, USB полностью во владении ядра, userspace-`pd-mapper` убран,
ADB/сенсоры/I2C на resume починены.

Каждый блок проверен командой на работающей системе:

| Блок | Команда → факт | Вердикт |
|---|---|---|
| CPU | `cpuinfo` → 8 ядер; `scaling_cur_freq` 0.94/2.2 ГГц | **FULL** — devfreq живой |
| DRAM | `free -m` → 2612 МБ доступно | **FULL** |
| UFS | `scsi_disk` → 6 LUN, model `SDINDDK4-64G`, state `running`, 17 sda-партиций | **FULL** |
| Панель | `drm card0-DSI-1 status` → `connected`, renderD128 | **FULL** |
| Подсветка | `/sys/class/backlight/5e94000.dsi.0` → 614/2047 | **FULL** — регулируется через DSI-панель |
| Тач | `fts_ts` → input4 на spi0.0 | **FULL** |
| bkl-bl / lcm-bias | i2c-карта → не пробиваются | **NOT SUPPORTED** — подтверждено |
| Камеры | `media-ctl` → 35 entities, 12 /dev/video*; i2c: s5kjn1, hi556, gt9764, 24c128, 24c64 | **NORMAL** — топология полная; OV02B1B в карте отсутствует |
| OV02B1B | i2c-карта → нет | **LOW** — подтверждено |
| Звук | `aplay -l` → card0 MultiMedia1; swr-master ×2 + slave ×2 | **FULL** |
| Wi-Fi | `iw dev` → connected Xiaomi_E4C4 **5GHz/5220MHz**, RX 23 МБ | **FULL** — реальный трафик |
| BT | `hciconfig` → hci0 **UP RUNNING**, Powered | **NORMAL** |
| NFC | i2c-карта → nq-nci не пробивается | **LOW** — подтверждено |
| Зарядка/гейдж | `power_supply` → battery `Charging`; `bms`: OCV 4419 мВ, `CHARGE_FULL_DESIGN` 5096 мАч (SOH 93.8%), энергия, внутр. темпа | **FULL** |
| USB (kernel-owned) | `/proc/iomem` → `04ef8800-04ef8bff : 4ef8800.usb` (QSCRATCH за драйвером), UDC `4e00000.usb` | **FULL** |
| Suspend/resume | `s2idle` (проверено с пробуждением по WoWLAN magic packet): ADB (`usb-adb-resume`), сенсоры (`rain-sensor-resume`), BT (`rain-bt-suspend/-resume`), I2C-во-сне и **падение ADSP** (`rain-sensor-suspend`) починены; окно сна чистое | **FULL** |
| Сенсоры | `hexagonrpcd-adsp-rootpd`/`sensorspd` active, `/dev/fastrpc-adsp`, `iio:device0` | **NORMAL** |
| GPU | `/dev/dri` → renderD128; devfreq 320МГц→таблица до 1.1ГГц | **FULL** — транзишны идут |
| Модем | `mmcli` → MPSS.HA отвечает, qrtr0+rmnet_ipa0; `failed: sim-missing` | **NORMAL** — стек жив; SIM не вставлена |
| remoteproc | modem `running`, adsp `running` | **FULL** |
| IPA | `rmnet_ipa0` DOWN (нет bearer без SIM) | **NORMAL** — интерфейс существует |
| tsens | tz0–3 → battery 32.8°C, cpu 36°C — живые значения | **FULL** |
| VADC | iio:device0 → 14 каналов | **FULL** |
| RTC | `hwclock` → 1970 (читает, не пишет) | **LOW** — как на стоке |
| Сенсоры | `hexagonrpcd` ×2 running, `-R` root присутствует; iio-sensor-proxy inactive | **NORMAL** |
| Вибратор | `gpio-vibrator` input0 | **FULL** |
| Джек | `Xiaomi-Redmi10C Headset Jack` input | **FULL** |
| SD | `mmcblk0` 30.4М блоков — карта видна | **FULL** |
| USB-C | `typec port0` + `port0-partner` | **NORMAL** |
| Вспышка | `leds/white:torch` нода есть | **NORMAL** |
| Fingerprint | нет сервиса/ноды | **NOT SUPPORTED** |
| GPIO/TLMM | 3 gpiochip, 80 линий | **FULL** |

## Сводка поддержки (mainline; boot_b обновлён 2026-10-18 — полный fuel gauge)

- **FULL**: панель, тач, UFS, SD, DRAM, GPU, display-стек, Wi-Fi,
  звук (динамик+гарнитура+джек), зарядка, fuel gauge (SBS +
  gaugeinfo: SOH/энергия/OCV/внутр. темпа — см. `FUEL-GAUGE-SH366101.md`),
  вибромотор, кнопки, подсветка, WL2866D/EEPROM камер, базовые
  SoC-блоки (pinctrl, SPMI, GENI, SoundWire, tsens, CCI, USB).
- **NORMAL**: SoC в целом, PMIC, камеры (нет AE/AWB), модем/LTE
  (data есть, voice/SMS частично), Bluetooth (редкий wedge),
  ADSP/LPASS (стартовый AFE-шум), IPA, CAMSS, interconnect/clk-
  quirks, RTC (read-only), USB-C роли, SIM-1, микрофоны/ресивер,
  вспышка.
- **LOW**: depth-камера (не проверена), SAR ADUX1050, NFC (только
  id), SIM-2, ant-check GPIO, CDSP-нода.
- **NOT SUPPORTED**: сканер отпечатков (FPC), FUSB303, bkl-bl,
  lcm-bias, fs15xx (audio smart-boost — драйвера в дереве нет).
