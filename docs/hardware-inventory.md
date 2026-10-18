# Инвентарь железа Redmi 10C (fog/rain)

Снято с живого устройства из Android (слот A, root/Magisk):
getprop, dumpsys sensorservice/media.camera, sysfs.
Модель: **220333QNY** (rain, NFC-вариант), `ro.board.platform=bengal`,
DT-модель: `RAIN KHAJE IDP nopmi`.

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
