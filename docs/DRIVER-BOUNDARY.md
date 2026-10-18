# Граница kernel ↔ userspace: что где живёт и что можно унести в ядро

Дата: 2026-10-18. Анализ по факту (файлы, конфиг, живые проверки).

## 1. Три слоя

| Слой | Где | Роль |
|---|---|---|
| Ядро | `out/src/linux` + `kernel.patch` | драйверы, DT, quirks |
| Initramfs | `initramfs/` | rescue-шелл (`display-console-rescue-init`), pivot-init (mount cust → switch_root), прошивки, daemons |
| Rootfs (Ubuntu) | `rootfs/tools/rain-overlay/` + `sensors/` + `vendor-qcom/` | systemd-юниты, glue-скрипты, внешние бинарники (hexagonrpcd из pmOS) |

## 2. Ядро: что наше

### Новые файлы (написаны с нуля)

| Файл | Строк | Что | Стиль |
|---|---|---|---|
| `drivers/power/supply/sh366101_fg_bringup.c` | 535 | fuel gauge SH366101: SBS + gaugeinfo-блоки | upstream-style |
| `drivers/power/supply/smb1351_charger_bringup.c` | 764 | зарядник SMB1351 | upstream-style |
| `drivers/pinctrl/qcom/pinctrl-khaje.c` | 1663 | pinctrl SM6225 (+`reserved_gpios`) | upstream-style |
| `drivers/media/i2c/ov02b1b.c` | 975 | depth-камера OV02B1B | upstream-style |
| `drivers/gpu/drm/panel/panel-xinli-ft8006s.c` | 340 | панель FT8006S | upstream-style |
| `drivers/misc/aw87xxx-mini.c` | 192 | PA Awinic (урезанный) | mini/bringup |
| `drivers/misc/wl2866d.c` | 96 | camera PMIC WL2866D | mini/bringup |
| `drivers/input/touchscreen/fts_spi/` | ~10k | тач Focaltech | **вендорный форк** |

### Модифицированные подсистемы (69 файлов в патче)

Кластеры: clk (GCC/GPUCC/DISPCC sm6115/6375, alpha-pll, bulk), interconnect
(icc-rpm keep-alive, sm6115), remoteproc/mdt_loader, UFS (core + qcom + PHY),
USB (dwc3 core/gadget, qusb2, snps-femto, wcn3990-bt/hci_qca), IPA, CAMSS
(CSIPHY 3ph + wiring), soundwire + wcd937x + lpass-макросы + q6core/q6asm,
pinctrl-msm (reserved_gpios, irq_quiesce), ath10k wmi guard, i2c-qcom-geni
(`qcom,no-dma`), qcom_pd_mapper (sm6225 domains), scsi/sd + block/partitions
(UFS-quirks), socinfo/ubwc.

### Конфиг: что built-in

`ATH10K_SNOC=y`, `BT_HCIUART_QCA=y`, `QRTR=y`, `QRTR_SMD=y`,
`QCOM_Q6V5_PAS=y`, `QCOM_Q6V5_MSS=y`, `QCOM_SYSMON=y`, `QCOM_MDT_LOADER=y`,
`QCOM_PD_MAPPER=y`, `QCOM_SOCINFO=y`, весь звук `SND_SOC_QDSP6*`/`QCOM_COMMON`/
`WCD937X`/`SM8250` `=y`, `SH366101`/`SMB1351` `=y`.
Модули (`=m`): `QCOM_FASTRPC`, `QCOM_Q6V5_ADSP` (не наш ADSP — см. ниже),
`QCOM_PM8916_WATCHDOG`.

## 3. Userspace: что делает каждый компонент

| Компонент | Слой | Что делает | Природа |
|---|---|---|---|
| `pivot-init`, `display-console-rescue-init` | initramfs | USB PHY MMIO, ACM-гаджет, mount cust | **workaround + glue** |
| `pd-mapper`, `rmtfs`, `tqftpserv`, `qrtr-ns` | initramfs + rootfs | QRTR-сервисы для DSP/модема (защитные домены, ФС модема, TFTP-конфиг) | протокол (by design) |
| `qcom-firmware-stage.sh` | rootfs | mount modem_a, симлинки прошивок, rmtfs-линки | **packaging-glue** |
| `qcom-modem-start.sh` | rootfs | `echo start > remoteproc/state`, отключение recovery | glue (обязателен: auto_boot=false) |
| `ath10k-snoc-load.sh` | rootfs | settle 3с + modprobe + wlan0 up | **workaround** |
| `rain-audio-start.sh` | rootfs | modprobe звуковых модулей | **избыточен** (см. P3) |
| `rain-bt-start.sh` | rootfs | modprobe hci_uart, hciconfig up, старт pulse | glue |
| `rain-stable-boot.sh` | rootfs | kill stale, reset modem rproc, rmmod ath10k, снятие масок | **workaround-сборка** |
| `usb-adb-gadget.sh` | rootfs | configfs-гаджет ADB+ACM, (мёртвый) PHY-пьок | glue + workaround |
| `uim-provision.sh`, `uim-hotplug.sh` | rootfs | QMI/UIM: бинд USIM-сессии, slot2 off, hot-swap | протокол модема |
| `rain-gps-start.sh`, `gnss-keepalive`, `rain-loc-tool` | rootfs | QMI LOC/PDS, A-GPS-бандл, NMEA | протокол модема |
| `qbootctl-mark.sh` | rootfs | Android bootctrl: пометить слот | bootctrl (by design) |
| `nm-dispatcher-dummy.py` | rootfs | заглушка NM-dispatcher | glue |
| `sensors-setup`, `hexagonrpcd`, `ssccli` | rootfs + `sensors/` | SSC: SNS-registry с persist, fastrpc, сенсоры через DSP | протокол DSP (by design) |
| `modprobe.d/rain-stable.conf` | rootfs | blacklist драйверов | **мёртвый файл** (см. P2) |

## 4. Кандидаты на перенос в ядро (по приоритету)

### P1. USB PHY / QSCRATCH MMIO → в драйверы PHY/dwc3

Оба initramfs-скрипта и gadget-скрипт дёргают регистры `devmem`-ом:

| Адрес | Что | Где в ядре должно быть |
|---|---|---|
| `0x141a004/010/014/018`, `0x141d000/008` | сброс/PLL/enable PHY (вне DT-нод) | PHY-драйвер (`phy-qcom-*`) |
| `0x4ef8808/810/830` | QSCRATCH: reset-последовательность (0x100→0x9→сброс) | `dwc3-qcom` |
| `0x161306c/070/074` | тюнинг HS PHY (0x61/0x08/0x2d) | `phy-qcom-snps-femto-v2` (или `qcom,phy-init-sequence` в DT) |
| `0x4e0c200` | DWC3 global (сброс бит 0x140) | `dwc3` core |

Мы уже правили `phy-qcom-snps-femto-v2.c`, `dwc3-qcom-legacy.c`, `dwc3/core.c` —
остаток (эти четыре группы регистров) логично добить там же, чтобы initramfs
перестал писать в MMIO. **Усилие:** среднее. **Риск:** средний (USB — путь
восстановления; тестировать только через `fastboot boot`).
**Побочный выигрыш:** снимается правило «не трогать USB PHY MMIO в цикле» и
мёртвый `RAIN_USB_PHY` в `usb-adb-gadget.sh`.

### P2. Удалить мёртвое: `modprobe.d/rain-stable.conf`

Каждое имя в blacklist — либо `=y` (blacklist не действует на built-in), либо
вообще не собран: `ath10k_snoc=y`, `qrtr=y`, `qrtr_smd=y`, `qcom_q6v5_pas=y`,
`qcom_sysmon=y`, `mdt_loader=y`, `sh366101_fg_bringup=y`,
`smb1351_charger_bringup=y`; `ath10k_pci/sdio`, `qmi_helpers` — не собраны.
Файл не влияет ни на что → удалить (или переписать под реальные `=m`).

### P3. `rain-audio-start.sh` — избыточен

ADSP у нас `qcom,sm6115-adsp-pas` → `adsp_resource_init` с
`.auto_boot = true` (`qcom_q6v5_pas.c`) → **ADSP поднимается ядром сам**.
Весь звук `=y`. `modprobe qcom_q6v5_adsp` в скрипте — вообще другой драйвер
(`QCOM_Q6V5_ADSP=m`, для qcs404-подобных). Скрипт можно удалить вместе с
юнитом, оставив только (если подтвердится) фикс гонки pinctrl/`q6afecc`.

### P4. `ath10k-snoc-load.sh` → свести к нулю

`ATH10K_SNOC=y`: `modprobe` — no-op, ожидание `wlan0` — только settle-пауза
под зависания DSP. Если после P1/P5 проба ath10k стабильна, скрипт удаляется;
остаётся зависимость «модем/WLAN-DSP up → ath10k probe», которую должно
держать ядро (deferred probe), а не sleep 3.

### P5. In-kernel `pd-mapper` vs userspace `pd-mapper`

`CONFIG_QCOM_PD_MAPPER=y` + наша правка (домены `qcom,sm6225`) — ядро умеет
отвечать на QRTR-запросы PD. Userspace `pd-mapper` крутится параллельно.
Если после удаления юнита ADSP/hexagonrpcd/звук продолжают работать —
userspace-демон лишний. **Нужен эксперимент** (стоп юнита + рестарт
hexagonrpcd).

### P6. `qcom-firmware-stage.sh` — сократить

Прошивки уже лежат в `/lib/firmware` (initramfs кладёт `adsp.*`, `modem.*`,
`a610_zap`, `ipa_fws`, ath10k board-2). Остаётся runtime-логика: симлинки
`board.bin` из раздела modem_a, `mcfg.tmp`-заглушки для tqftpserv, rmtfs-линки.
Часть можно вынести в сборку rootfs (положить нужные BDF в образ), часть —
в udev-правила вместо скрипта.

### P7. `sensors-setup`: socinfo — из ядра, не хардкодом

Скрипт пишет `soc_id=518/revision/hw_platform=...` руками. Ядро уже отдаёт
это в `/sys/devices/soc0/` (`soc_id=518`, `machine=Xiaomi Redmi 10C fog/rain`).
Заменить хардкод на копирование из `/sys/devices/soc0` (устойчиво к смене
платформы).

### P8. Драйверы, которых в ядре нет (написать)

| Чип | Что есть в mainline | Что нужно | Усилие |
|---|---|---|---|
| FUSB303 (USB-C CC) | нет | небольшой typec/CC-драйвер | низкое |
| ADUX1050 (SAR) | только `adux1020` | IIO-драйвер (если шина AP-видна — см. §5) | среднее |
| TMD2755 (ALS+prox) | только `tsl2772`-семейство | IIO light/prox драйвер | среднее |
| NFC pn8x | `nxp-nci` (PN547/553) | compat + quirk; стек (neard) всё равно нужен | среднее |
| FPC сканер | нет | полноценный драйвер (сейчас нет вообще) | высокое |

## 5. Что перенести в ядро НЕЛЬЗЯ (и почему)

- **Сенсоры SC7A20 / TMD2755 / ADUX1050** — физически на SSC-шине (в i2c-карте
  Android и Ubuntu их нет; обслуживаются SLPI через fastrpc). Kernel-IIO
  невозможен без смены владельца шины в DSP-прошивке. Забавно: mainline
  **уже** знает `silan,sc7a20` (`st_accel_i2c.c`) — но здесь он бесполезен.
- **rmtfs / tqftpserv / qrtr-ns / hexagonrpcd** — часть архитектуры Qualcomm:
  DSP/модем ходят в userspace-сервисы по QRTR. В ядре им места нет
  (in-kernel `pd-mapper` — исключение, см. P5).
- **UIM/QMI (uim-provision, uim-hotplug), GNSS (rain-gps-start, gnss-keepalive),
  ModemManager/NM** — протоколы модема и сетевого стека.
- **configfs-гаджет (`usb-adb-gadget.sh`)** — configfs по определению
  userspace; можно лишь убрать из него MMIO-костыль (P1).
- **`qbootctl-mark.sh`** — Android bootctrl (слоты A/B).
- **Прошивка тача** — уже в ядре (`request_firmware` в `fts_spi/focaltech_flash.c`).

## 6. Итог одной таблицей

| Подсистема | Сейчас | Куда двигать |
|---|---|---|
| USB PHY/QSCRATCH init | userspace `devmem` | **→ ядро (P1)** |
| ADSP boot | ядро (auto_boot) + лишний скрипт | **удалить скрипт (P3)** |
| Звук (ASoC) | ядро `=y` | ок |
| Модем boot | userspace (by design) | ок (auto_boot=false) |
| WLAN probe | ядро `=y` + settle-скрипт | **сократить (P4)** |
| pd-mapper | ядро `=y` + userspace демон | **тест на удаление (P5)** |
| Прошивки | initramfs + staging-скрипт | **в сборку rootfs (P6)** |
| Сенсоры | userspace SSC | нельзя (SSC-шина) |
| Fuel gauge / зарядник | ядро (полные) | ок |
| Тач (прошивка) | ядро | ок |
| SIM/GPS | userspace QMI | нельзя |
| USB-гаджет | userspace configfs | ок (убрать MMIO) |
