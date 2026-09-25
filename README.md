# Kernel_Redmi10c

Чистый минимальный набор для сборки и тестирования mainline-ядра под Xiaomi Redmi 10C (`rain`/`fog`, Qualcomm SM6225 "Khaje").

## Поддерживаемые устройства

| Устройство | Codename | SoC | DTS |
|---|---|---|---|
| Xiaomi Redmi 10C | `fog` | SM6225 (Khaje) | `sm6225-xiaomi-fog.dts` |
| Xiaomi Redmi 10C / Redmi 10 (India) | `rain` | SM6225 (Khaje) | `sm6225-xiaomi-rain.dts` |
| Xiaomi Redmi 10C (вариант SM6115+PM6125) | `fog` | SM6115 + PM6125 | `sm6225-xiaomi-fog-sm6115.dts` |

Собранный по умолчанию DTB — `sm6225-xiaomi-fog.dtb`. Проверено на живом
устройстве: **Redmi 10C (`fog`)**, слот `b`, через `fastboot boot` (без прошивки).

## Быстрый старт

```bash
# 1. зависимости (Debian/Ubuntu)
sudo apt install git make python3 cpio gzip xz-utils zstd \
  gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu adb fastboot

# 2. закрытые прошивки (ADSP/модем/WLAN/BT/GPU/тачскрин) — из GitHub-релиза
tools/fetch-firmware.sh

# 3. сборка: clone linux v7.1.5 -> kernel.patch -> kernel -> initramfs -> boot.img
./build.sh

# 4. загрузка на телефон (временно, ничего не прошивается)
fastboot set_active b && fastboot reboot bootloader
fastboot boot out/boot-display-console-rescue.img
```

Быстрая пересборка уже настроенного дерева — `tools/rebuild-rescue.sh`;
загрузка + проверка состояния — `tools/boot-and-check.sh`.

## Что внутри

| Файл/папка | Назначение |
|---|---|
| `kernel.patch` | Единый патч поверх Linux `v7.1.5` с USB/PHY/DWC3/TypeC/UFS/pinctrl/clk/Bluetooth фиксами, bringup-драйверами зарядки/АКБ (`smb1351`, `sh366101`) и тачскрином (`fts_spi`) |
| `kernel.config` | Рабочий `.config` (USB/PHY/UFS/gadget включены, драйверы отключены по минимуму) |
| `initramfs/display-console-rescue-init` | Initramfs: shell на экране (`/dev/console`) и через USB ACM (`/dev/ttyGS0`), без rootfs |
| `initramfs/busybox.config` | Конфиг для статической сборки busybox под aarch64 |
| `build.sh` | Один скрипт: clone → patch → build kernel → build initramfs → pack boot image |
| `out/` | Локальные исходники, сборка и готовый образ (не коммитятся) |

## Проверено на устройстве

Проверенный локальный образ `out/boot-display-console-rescue.img`:
- Загружается временно через `fastboot boot ...` (без постоянной прошивки).
- Поднимает USB gadget ACM (`/dev/ttyACM0` на хосте).
- Shell отвечает через `screen /dev/ttyACM0 115200` (при первом подключении может потребоваться `Ctrl-C` для получения приглашения).
- UFS определяется, ядро видит партиции.
- **GPU (Adreno 610) работает**: `CONFIG_DRM_MSM=y`, включены `gpu`/`gpucc`/`gmu`/`adreno_smmu` (SMMU больше не отключён), `mdss`/`dispcc`. `/dev/dri/card0` + `renderD128`, devfreq `simple_ondemand`, частоты 465/600/785/820/980 МГц, PLL0 (ZONDA, 930 МГц) → OUT_MAIN (465 МГц). ZAP-шейдер грузится из `qcom/sm6225/a610_zap.mdt` (+`.b00/.b01/.b02`). Разбор и грабли — `docs/GPU-BRINGUP.md`.
- `arm-smmu 59a0000.iommu` (Adreno/GPU SMMU) **включён** (был отключён до GPU bringup).
- Пересечение `reserved-memory` framebuffer и Android carveouts убрано — `OVERLAP DETECTED` исчез.
- `regulatory.db` включена в initramfs — `cfg80211` больше не ругается на firmware.
- UFS regulator warnings (`vdd-hba-supply`, `vccq-supply`) убраны.
- Wi-Fi `vdd-3.3-ch1` и vibrator dummy-regulator warnings убраны.
- Шумное логирование regulator core и fw_devlink dependency cycles приглушено (`rdev_info`/`pr_info` → `rdev_dbg`/`pr_debug`).
- Спам UFS/SCSI (`Well-known LUN`, `logical blocks`, `Write cache`, `Write Protect`, `Attached SCSI disk`, список партиций `sda: sda1 ...`) убран — `sdev_printk`/`sd_printk` → `sdev_dbg`/`pr_debug`.
- В cmdline добавлен `quiet loglevel=3` — на экран/консоль идут только warnings/errors; `dmesg` по-прежнему полный.
- Остаются **только 2 firmware/bootloader предупреждения**, не фиксимые в ядре:
  - `[Firmware Bug]: Kernel image misaligned at boot, please fix your bootloader!`
  - `psci: [Firmware Bug]: failed to set PC mode: -3`
- Из `.config` вырезаны мусорные built-in драйверы: Intel (e1000/e1000e/igb/igbvf/ixgbe/i40e/ice), Marvell (sky2, mvneta, mvpp2), Hisilicon HNS/HNS3, ThunderX, SMSC, TI, Freescale ENETC, TUN/TAP/VETH/VIRTIO_NET и др.
- Дополнительно убраны: PCI/PCIe host controllers, USB host HCD, USB HID/storage, SATA/PATA/RAID, MMC/SD, звук (SND), IIO, media, HID-периферия других вендоров.
- Отключены все vendor-платформы в `Platform selection` — оставлен только `ARCH_QCOM`. Вместе с ними выпали чужие clock/pinctrl/soc-драйверы (`hisi_rng`, `imx_sm_bbm_key`, `pinctrl_bcm2835` и т.п.).
- Убраны драйверы без `depends on ARCH_*`: XEN, CAN, NET_DSA, ChromeOS EC, USB-net адаптеры, ethernet PHY/MAC (`QCOM_EMAC` в т.ч.), Broadcom Wi-Fi/BT, чужие MFD-PMIC (AXP20X, RK8XX, MT63xx, TPS*, ROHM, SEC, HI6421), MTD flash (CFI/PHYSMAP/DENALI/SPI_NOR), TPM TIS, TI/NXP/Cadence/misc.
- Выключены целые мёртвые подсистемы: `CONFIG_ACPI` (DT-only устройство), `CONFIG_KVM`/`VIRTUALIZATION`/`PARAVIRT`, `CONFIG_HIBERNATION`, IPMI, PS/2-стек (`MOUSE_PS2`/`KEYBOARD_ATKBD`), USB host `ISP1760`, ULPI.
- Результат: `dmesg` сократился с ~2200 строк до ~316 строк, образ с 17.61 MiB до 7.81 MiB; никаких ошибок/timeout/deferred/sync_state. Из конфига выкинуто всё лишнее: DRM/MEDIA/NFS/9P/ATA/SAS/MTD/8250-serial/VIRTIO/KEXEC/AUDIT/PERF/BPF/SCMI/IPV6/NETFILTER, лишние USB-gadget-функции, чужие clock/pinctrl/interconnect-драйверы других SoC. Wi-Fi стек (CFG80211/MAC80211) оставлен `=m` — не попадает в Image. ⚠️ `QCOM_CLK_SMD_RPM` и `SM_GPUCC_6115` обязательны: rpmcc/gpucc — реальные провайдеры клоков в sm6225.dtsi, без них всё висит на EPROBE_DEFER.
- UFS по-прежнему работает и видит диск/партиции (проверено через `/dev/sd*` и `dmesg`).
- Зарядка и АКБ работают: включён `&i2c2`, добавлены минимальные bringup-драйверы `smb1351` (charger, SE2 `i2c@4a88000`) и `sh366101` (fuel gauge, SE1 `i2c@4a84000`). `/sys/class/power_supply/` содержит `battery`, `bms`, `usb`; статус `Charging`/`Full`, SOC, напряжение, ток и температура читаются. Software Jeita по температуре из `bms`.
- Заодно ожив Type-C контроллер `wusb3801` на `i2c2` — `port0`/`port0-partner` видны в `/sys/class/typec/`.
- Лог почищен: убран `earlycon`/`stdout-path` (ранил `earlycon: stdout-path framebuffer0 not found`), `irq-gpios`/`check boot id`/`no irq_gpio` переведены в FTS_INFO (нормальный путь — драйвер берёт `spi->irq`), добавлен `vccq-max-microamp` на ufshc, вибратору дан always-on `vib_vcc` fixed-regulator (убирает `supply vcc not found`), в `fts_ts_id` добавлен `{"fts"}` (убирает `no spi_device_id` warning), прототипы в `focaltech_core.h` разблокированы из `#if`-гардов + добавлены `fts_gesture_switch`/`set_*` — сборка без `-Wmissing-prototypes`. В dmesg остались только info-строки.
- Thermal работает: в `sm6225.dtsi` добавлена `tsens0` (SROT@0x4410000/TM@0x4411000, SPI 275/190 — идентично sm6115, подтверждено дампом Android `/proc/device-tree`) + 4 thermal-zones с индексами сенсоров из вендорного DT (cpu=6, cpuss=10, mdm=13, gpu=15). Живые температуры в `/sys/class/thermal/`. Cooling-maps пока нет (нет cpufreq cooling device) — только мониторинг.
- Vibrator работает и проверен физически: `gpio-vibrator` на TLMM gpio36, input `event0` с FF rumble; `vibtest /dev/input/event0 <ms>` в initramfs дёргает мотор через настоящий EVIOCSFF/EV_FF ioctl (статический aarch64 бинарь, не MMIO-поки — devmem по TLMM на этом чипе вешает шину). Важно: **нельзя** давать ноде pinctrl с gpio36 — gpiod и pinctrl дерутся за пин (`pin already requested` → probe -EINVAL). `vcc-supply` указывает на always-on `vib_vcc` fixed-regulator.
- Тачскрин end-to-end проверен: реальные evdev-события с координатами/давлением (`MT_X=360 MT_Y=903 MT_PRESS=37..51 BTN_TOUCH=1`), IRQ fts_ts инкрементится при касаниях. Input-нода: `/dev/input/event4` (`fts_ts`, может сдвигаться при добавлении устройств).
- Термистор вспышки работает: `pm6125_adc` + канал `camera_flash_therm` (ADC5_GPIO1_100K_PU, ch 0x52, 100K-PU, ratiometric); читается как `in_temp_camera_flash_therm_input` в миллиградусах (~33°C). `CONFIG_IIO=y` + `QCOM_SPMI_ADC5=y`.
- SD-карта работает: `sdhc_2` (mmc@4784000, `sdhci-msm-v5`) + `CONFIG_MMC_SDHCI_MSM=y`; QDSD-пады 117-119, `broken-cd` (CD gpio88 в reserved). Карта видна как `/dev/mmcblk0`, SDR104, чтение 77 MB/s.
- **Звук (динамик) работает** — mainline-стек доведён до воспроизведения, вендор-паритет по SWR и кодеку:
  - `swr0` (RX-мастер `a610000`): был неверный `iface`-клок (`q6afecc LPASS_HW_DCODEC_VOTE`), а гейт SWR-клока живёт в RX-макро (`swclk_gate_enable` → `CDC_RX_CLK_RST_CTRL_SWR_CONTROL`). Исправлено на `<&lpass_rx_macro>` + `resets = <&lpass_audiocc LPASS_AUDIO_SWR_RX_CGCR>`; добавлен `qcom,swrm-hctl-reg` (HCTL/div2 для v1.6.0) обоим мастерам.
  - DAI-link `WCD Playback`: `RX_CODEC_DMA_RX_1` ↔ `<&lpass_rx_macro 1>` (AIF2). Было `lpass_rx_macro 0` (AIF1) → DAPM-путь AIF не питался, DSP не сливал буфер (`hw_ptr=0`).
  - **Форма кадра SWR как у вендора** (`bengal-port-config.h`/`swr-mstr-ctrl.c`, khaje): `rows=50, cols=2` + поле `SSP_PERIOD=47` в `FRAME_CTRL` (mainline писал `cols=16` и не писал SSP вообще). Именно это убрало «хрип/фарш».
  - **Порты SWR** приведены к вендорским khaje: `sinterval-low=<3 31 31 7 0>`, `offset1=<1 0 11 1 0>`, `lane-control=<0 0 0 0 0>`.
  - `q6asm_dai_ack()`: защита от underflow `appl_ptr - queue_ptr` (после XRUN разность уходила в минус → ~750k «буферов» → runaway → soft lockup).
  - `RX HPH Mode`: исправлена таблица enum (была смещена на 1 — выбор «CLS_AB» реально включал `CLS_H_LOHIFI`); дефолт `CLS_H_ULP` (как у вендора). Добавлен контроль `AUX PATH Mode` (HP/NORMAL) = бит 6 `WCD937X_DIGITAL_CDC_PATH_MODE`.
  - AUX-PA кодека приведён к вендору (убраны лишние биты 6/7 в `ANA_RX_SUPPLIES`, `PDM_WD_CTL2=0x05`); таблица reg-дефолтов кодека — 2 значения к вендору (`ANA_EAR=0x40`, `MBHC_TEST_CTL=0x30`). class-H у нас и так идентичен вендорскому.
  - PA `aw87xxx-mini`: профиль Music = вендорские рантайм-значения (`0x59=0x4A, 0x06=0x0E, 0x66=0x10, 0x67=0x23, 0x72=0xFE, 0x76=0x04, 0x77=0x00, 0x78=0x01`). `0x60` — динамический статус PA (не конфиг).
  - Надёжность: `pm_runtime_forbid` для SWR-мастера и кодека (повторные прогоны стабильны), `hung_task_panic=1`+`panic=15` в init (cmdline из boot-образа перекрывается `vendor_boot`), debugfs монтируется в init (доступен покер `wcd937x-reg`).
  - **Mute/unmute RX3** (как вендорское болеро-событие): `POST_PMU` снимает mute AUX-PGA, `PRE_PMD` глушит **до** снятия питания — заметно убрало щелчок и шум переходного процесса.
  - **PA в покое выключен** (как в Android): обёртка `/bin/play <файл>` включает усилитель на время воспроизведения и гасит после — постоянный шум усилителя в простое убран.
  - **Остаточный шум:** ВЧ-шипение (3–16 кГц) во время воспроизведения — это **аналоговая шумовая полка кодекса WCD937x**, которую усиливает PA; в Android её нет из-за калибровки **ACDB** (грузится userspace-HAL). Полный разбор — `docs/audio/NOISE-ANALYSIS.md`.
- Стабильность прогнана (лог `logs/stress-soak-test.log`): CPU burn 8 ядер × 60с (load 5.06, пик 41°C), md5-hammer 4×45с на 256MB, UFS read storm 40с, 1.2GB tmpfs fill — без OOM/сталлов. 15-мин idle soak: 15/15 сэмплов ноль RCU stalls, температурный дрейф <1.5°C, зарядка 96→98%. **Известный триггер зависания:** `devmem` по TLMM gpio-регистрам вешает шину (сырой MMIO к неразрешённой зоне) — не трогать; с шеллом/FF-ioctl работает всё.
- Тачскрин работает: downstream-драйвер `fts_spi` (Focaltech FT8006S, Xinli) в `drivers/input/touchscreen/fts_spi/` — probed по spi-gpio bitbang, `event3` с multitouch ABS, IRQ gpio80 флудит при касаниях. **Важно:** IC не имеет постоянной app-прошивки — грузится в boot mode, драйвер заливает `focaltech_ts_fw_xinli.bin` в PRAM при каждом probe (как в Android). Файл извлечён из `/vendor/firmware/` и лежит в `initramfs/lib/firmware/` → `/lib/firmware/` в образе.

## Звук: тест динамика

```sh
tinymix set 'RX_CODEC_DMA_RX_1 Audio Mixer MultiMedia1' 1
tinymix set 'RX_MACRO RX2 MUX' AIF2_PB
tinymix set 'RX INT2_1 MIX1 INP0' RX2
tinymix set 'AUX_RDAC Switch' 1
tinymix set 'LO Switch' 1                 # SWR-порт 4 (AUX/RDAC4)
tinymix set 'AUX_HPF Switch' 1
tinymix set 'AUX PATH Mode' HP_MODE       # бит 6 WCD937X_DIGITAL_CDC_PATH_MODE
tinymix set 'RX HPH Mode' CLS_H_ULP       # вендорский дефолт class-H
tinymix set 'RX_RX2 Digital Volume' 84    # 0 дБ (совпадает с вендорским runtime)
play /SAO-20s-mono16-norm.wav             # PA вкл/выкл автоматически (как в Android)
```

`play` — обёртка над `tinyplay`: включает PA (`mode=music`) только на время
воспроизведения и выключает после, поэтому в покое динамик молчит.

Тестовые файлы в образе: `/SAO-20s-mono16-norm.wav` (60%), `-mid` (25%), `-mono16.wav` (5%),
`/SAO-20s-silence.wav` (чистая тишина), `/SAO-20s-quiet.wav` (±1 LSB),
`/test-1k.wav`, `/test-1k-mono.wav`. Пустая дорожка без файла — `pcm-hold <сек>`.

Отладка: `/sys/kernel/debug/wcd937x-reg` (покер регистров кодека, debugfs монтируется в init):
```sh
echo 3128 > /sys/kernel/debug/wcd937x-reg        # прочитать -> dmesg | grep wcd937x-dbg
echo "30b4 1" > /sys/kernel/debug/wcd937x-reg    # записать
```

## Требования к хосту

```bash
sudo apt install git make python3 cpio gzip \
  gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu \
  adb fastboot
```

## Прошивки (закрытые блобы)

Проприетарные прошивки **не хранятся в git** (покрыто `.gitignore`). Они лежат
asset'ом в релизе основного репо и подтягиваются скриптом:

```bash
tools/fetch-firmware.sh    # скачать firmware-<date>.tar.zst и распаковать в initramfs
tools/pack-firmware.sh     # (обратное) собрать архив прошивок для нового релиза
```

Релиз: `https://github.com/Damir00109/Redmi10c/releases/tag/firmware-2026.09.25`
(asset `redmi10c-firmware.tar.zst`: ADSP, модем, WLAN/BT, GPU (ZAP/GMU/SQE),
тачскрин FocalTech, аудио-усилители).

Переопределяется переменными `FIRMWARE_REPO` / `FIRMWARE_TAG`.

## Сборка

```bash
cd Kernel_Redmi10c
./build.sh
```

Результат: `out/boot-display-console-rescue.img` (~11.5 MiB).

## Загрузка на телефон

Телефон должен быть в fastboot. **Важно:** active slot должен быть `b`, иначе Android-dtbo из активного слота наложится на mainline DTB и загрузка сломается.

```bash
fastboot set_active b
fastboot boot Kernel_Redmi10c/out/boot-display-console-rescue.img
```

## Подключение к shell

Через USB ACM:

```bash
screen /dev/ttyACM0 115200
```

На экране телефона тоже должен быть терминал (fbcon).

## Возврат на Android

```bash
fastboot set_active a
fastboot reboot
```

slot `a` остаётся нетронутым fallback.

## Следующий шаг

Для полноценной Ubuntu rootfs нужно заменить initramfs на `tools/pivot-init` и добавить `root=PARTLABEL=cust rw rootwait` в cmdline в `build.sh`.
