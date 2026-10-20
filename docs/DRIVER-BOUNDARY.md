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
| `drivers/power/supply/sh366101_fg.c` | 535 | fuel gauge SH366101: SBS + gaugeinfo-блоки | upstream-style |
| `drivers/power/supply/smb1351_charger.c` | 764 | зарядник SMB1351 | upstream-style |
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

### P1. USB PHY / QSCRATCH MMIO → в драйверы PHY/dwc3 — **СДЕЛАНО (2026-10-18)**

Перенесено в ядро, все userspace-пьоки удалены (в `pivot-init` MMIO нет вообще):

| Что было в userspace | Куда ушло | Проверка |
|---|---|---|
| `0x4ef8808/810/830` QSCRATCH | нода-обёртка `usb@4ef8800` (`qcom,sm6115-dwc3`) → `dwc3-qcom` | `/proc/iomem`: регион за драйвером; `qs10=0x10100000` до бинда |
| `0x141a010/014/018`, `0x141d000/008` GCC | клоки в обёртке (`clocks`) + `CLK_IS_CRITICAL` | USB поднимается без пьоков |
| `0x161306c/70/74` HS PHY оверрайды | дефолты PHY-драйвера | значения совпали: `0x61616161/0x08080808/0x2D2D2D2D` |
| `0x4e0c200` GUSB2PHYCFG0 | `snps,dis_u2_susphy_quirk`/`dis_enblslpm_quirk` | — |
| `0x4e0c704` DCTL (RUN_STOP) | не нужен (был маскировкой бага) | bind без него работает |

**Главная находка при переносе:** `dwc3-qcom` выбирает UTMI-клок как PIPE-клок
**только** при `qcom,select-utmi-as-pipe-clk` (`dwc3_qcom_select_utmi_clk()`).
Без этого свойства первый bind гаджета падает с `dwc3: failed to enable ep0out`
(QSCRATCH_GENERAL_CFG без `PIPE_UTMI_CLK_SEL|PIPE3_PHYSTATUS_SW`), и устройство
не энумерится. Раньше это делал userspace-пьок. Свойство добавлено в обёртку —
порт HS-only контроллера.

Ловушка при тестировании: **`fastboot boot` маскирует cold-boot проблемы** — ABL
оставляет клоки USB включёнными для fastboot, поэтому образ работает после
`fastboot boot` и падает после `fastboot flash` + холодный ребут. Проверять
только холодным бутом.

### P2. Удалить мёртвое: `modprobe.d/rain-stable.conf` — **СДЕЛАНО**

Каждое имя в blacklist — либо `=y` (blacklist не действует на built-in), либо
вообще не собран: `ath10k_snoc=y`, `qrtr=y`, `qrtr_smd=y`, `qcom_q6v5_pas=y`,
`qcom_sysmon=y`, `mdt_loader=y`, `sh366101_fg_bringup=y`,
`smb1351_charger_bringup=y`; `ath10k_pci/sdio`, `qmi_helpers` — не собраны.
Файл не влиял ни на что → **удалён** (проверено ребутом: Wi-Fi/BT/зарядка/gauge
на месте).

### P3. `rain-audio-start.sh` — избыточен — **СДЕЛАНО**

ADSP у нас `qcom,sm6115-adsp-pas` → `adsp_resource_init` с
`.auto_boot = true` (`qcom_q6v5_pas.c`) → **ADSP поднимается ядром сам**.
Весь звук `=y`. `modprobe qcom_q6v5_adsp` в скрипте — вообще другой драйвер
(`QCOM_Q6V5_ADSP=m`, для qcs404-подобных). Скрипт и юнит **удалены** —
после холодного ребута card0 поднимается сам, ADSP `running`.

### P4. `ath10k-snoc-load.sh` → свести к нулю — **СДЕЛАНО**

`ATH10K_SNOC=y`: `modprobe` — no-op, ожидание `wlan0` — только settle-пауза
под зависания DSP. **Проверено ребутом:** с выключенным юнитом `ath10k_snoc` пробивается сам
(deferred probe ждёт WLAN DSP, в dmesg видно probe на ~41-й секунде), `wlan0`
поднимается и подключается к сети. Скрипт и юнит **удалены**; зависимость
«модем/WLAN-DSP up → ath10k probe» держит ядро.

### P5. In-kernel `pd-mapper` vs userspace `pd-mapper` — **СДЕЛАНО**

`CONFIG_QCOM_PD_MAPPER=y` + наша правка (домены `qcom,sm6225`) — ядро умеет
отвечать на QRTR-запросы PD. Userspace `pd-mapper` крутится параллельно.
**Проверено:** с остановленным userspace-демоном сенсорный PD-сервис
перезапускается, а модем поднимается (`qcom-modem-start` без зависимости) —
in-kernel маппер отдаёт `adsp_audio/root/sensor`, `cdsp_root`, `mpss_root_gps`,
`mpss_wlan` (таблица `sm6115_domains`). Юнит, скрипт-копия и зависимости
(`qrtr-ns`, `qcom-modem-start`, mount-override, `build-rootfs.sh`) **убраны**;
после ребута модем/ADSP/Wi-Fi/сенсоры работают, `pd-mapper` inactive.

### P6. `qcom-firmware-stage.sh` — сократить — **СДЕЛАНО**

Прошивки уже лежат в `/lib/firmware` (initramfs кладёт `adsp.*`, `modem.*`,
`a610_zap`, `ipa_fws`, ath10k `board-2.bin` + `firmware-5.bin`). Скрипт
**сокращён до минимума**: пустые `mcfg.tmp`/`lctoem.tmp` для tqftpserv,
readonly-дерево для абсолютных путей и rmtfs-линки по `PARTNAME`.

Убрано как лишнее (проверено ребутом: модем + WLAN + ath10k поднимаются):
mount `modem_a` и его зависимости, симлинки прошивок в `/run/modem_fw`
(их никто не читал), копии `wlanmdsp.mbn` (уже в образе), `modprobe rmtfs_mem`
(`=y`), симлинки `board.bin`/`firmware-5.bin`. Важное уточнение по последнему:
`firmware-5.bin` ath10k **нужен** (без него WLAN-процесс в модеме падает и
роняет весь модем: `err_qdi.c: EX:wlan_process`), но он уже приходит из
`initramfs/lib/firmware/` — старый скрипт лишь перезаписывал файл симлинком.
`board.bin` не нужен: ath10k берёт контейнер `board-2.bin`.

### P7. `sensors-setup`: socinfo — из ядра, не хардкодом — **СДЕЛАНО (частично)**

Скрипт пишет `soc_id=518/revision/hw_platform=...` руками. Ядро уже отдаёт
это в `/sys/devices/soc0/` (`soc_id=518`, `machine=Xiaomi Redmi 10C fog/rain`).
`soc_id`/`revision` теперь берутся из `/sys/devices/soc0`; `hw_platform`,
`platform_subtype`, `platform_version` остаются литералами — socinfo-драйвер
на этом SoC их не экспонирует.

### P8. Драйверы, которых в ядре нет — **разобрано, писать нечего/невозможно**

| Чип | Что есть в mainline | Что нужно | Усилие |
|---|---|---|---|
| FUSB303 (USB-C CC) | нет | **не отвечает на шине** — проверено сырым probe по всем 4 AP-шинам (0x21/0x22/0x25): NACK (`ENXIO`), в отличие от WUSB3801/smb1351, которые дают `EBUSY` как занятые драйверами. Драйверу не к чему биндиться, пока не выяснено, почему чип молчит (EN/питание/другой адрес) | — |
| ADUX1050 (SAR) | только `adux1020` | IIO-драйвер (если шина AP-видна — см. §5) | среднее |
| TMD2755 (ALS+prox) | только `tsl2772`-семейство | IIO light/prox драйвер | среднее |
| NFC pn8x | `nxp-nci` (PN547/553) | compat + quirk; стек (neard) всё равно нужен | среднее |
| FPC сканер | нет | полноценный драйвер (сейчас нет вообще) | высокое |

## 4a. Остаток userspace-«драйверного» кода (миграция завершена)

После P1–P7 в userspace не осталось ни одного MMIO-пьока и ни одной
подмены ядра:

- `usb-adb-gadget.sh` — мёртвый `RAIN_USB_PHY`/`usb_phy_once` (QSCRATCH) удалён,
  в скрипте остался только configfs;
- rescue-init больше не поднимает userspace `pd-mapper` (его нет и в
  initramfs-образе) — Wi-Fi-рецепт rescue проверен холодным бутом: ath10k
  пробуется на 9–12 с, `wlan0` создаётся;
- `rain-stable-boot.sh` делает только гигиену (pkill/rmmod/маски), MMIO нет;
- **в userspace не осталось ни одного `modprobe`/`insmod`**: `fastrpc` стал
  встроенным (`CONFIG_QCOM_FASTRPC=y`, проверено: `/dev/fastrpc-adsp` есть при
  пустом `lsmod`), а `hci_uart`/`libcomposite` и так были `=y` — строки-пустышки
  удалены;
- **tqftpserv/rmtfs/qrtr-ns — что откуда берётся** (проверено): `rmtfs` и
  `qrtr-ns` — из apt-пакетов (`/usr/bin/*`, юниты указывают туда), а
  `tqftpserv` — наша копия из initramfs-стейджинга (`/usr/local/sbin/tqftpserv`,
  юнит указывает туда). Лишние копии `rmtfs`/`qrtr-ns` в `/usr/local/sbin` и
  apt-пакет `protection-domain-mapper` (заменён in-kernel маппером) убраны из
  сборки; мёртвый `initramfs/pd-mapper` удалён из репозитория. В rescue-образе
  все четыре бинарника по-прежнему нужны (`/bin/*`, switch_root не делается);
- `rain-bt` удалён (как `rain-audio`): `hci0` поднимается сам
  (`hci_uart` built-in + bluetoothd), проверено холодным ребутом. Его побочная
  работа — запуск PulseAudio — перенесена на сам `pulseaudio.service`
  (был `disabled`, теперь `enabled`).

## 5. Что перенести в ядро НЕЛЬЗЯ (и почему)

- **Сенсоры SC7A20 / TMD2755 / ADUX1050** — физически на SSC-шине (в i2c-карте
  Android и Ubuntu их нет; обслуживаются SLPI через fastrpc). Kernel-IIO
  невозможен без смены владельца шины в DSP-прошивке. Забавно: mainline
  **уже** знает `silan,sc7a20` (`st_accel_i2c.c`) — но здесь он бесполезен.
- **rmtfs / tqftpserv / qrtr-ns / hexagonrpcd** — часть архитектуры Qualcomm:
  DSP/модем ходят в userspace-сервисы по QRTR. В ядре им места нет
  (in-kernel `pd-mapper` — исключение, см. P5).
  **Проверено экспериментом (2026-10-18):** холодный бут с выключенным
  `tqftpserv` — `wlan0` не появляется вообще, `ath10k` даже не пробуется
  (WLAN-DSP не получил `wlanmdsp.mbn`: его отдаёт именно tqftpserv по TFTP/QRTR).
  Перезапуск модема при остановленном tqftpserv обманчиво «работает» — прошивка
  к тому моменту уже загружена; проверять только холодным бутом. То есть
  `pd-mapper` из userspace ушёл, а `tqftpserv`/`rmtfs`/`qrtr-ns` — нет.
- **UIM/QMI (uim-provision, uim-hotplug), GNSS (rain-gps-start, gnss-keepalive),
  ModemManager/NM** — протоколы модема и сетевого стека.
- **configfs-гаджет (`usb-adb-gadget.sh`)** — configfs по определению
  userspace; можно лишь убрать из него MMIO-костыль (P1).
- **`qbootctl-mark.sh`** — Android bootctrl (слоты A/B).
- **Прошивка тача** — уже в ядре (`request_firmware` в `fts_spi/focaltech_flash.c`).

## 6. Итог одной таблицей

| Подсистема | Сейчас | Куда двигать |
|---|---|---|
| USB PHY/QSCRATCH init | **ядро** (обёртка `usb@4ef8800` + `qcom,select-utmi-as-pipe-clk`) | **сделано (P1)** |
| ADSP boot | ядро (auto_boot), скрипт удалён | **сделано (P3)** |
| Звук (ASoC) | ядро `=y` | ок |
| Модем boot | userspace (by design) | ок (auto_boot=false) |
| WLAN probe | только ядро (deferred probe) | **сделано (P4)** |
| pd-mapper | только ядро (`QCOM_PD_MAPPER`) | **сделано (P5)** |
| Прошивки | `/lib/firmware` из initramfs; скрипт — только tqftpserv/rmtfs | **сделано (P6)** |
| Мёртвый blacklist | удалён | **сделано (P2)** |
| socinfo в sensors-setup | из `/sys/devices/soc0` | **сделано (P7)** |
| Сенсоры | userspace SSC | нельзя (SSC-шина) |
| Fuel gauge / зарядник | ядро (полные) | ок |
| Тач (прошивка) | ядро | ок |
| SIM/GPS | userspace QMI | нельзя |
| USB-гаджет | userspace configfs | ок (убрать MMIO) |
