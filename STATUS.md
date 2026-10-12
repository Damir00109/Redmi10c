# Статус bring-up

Последнее обновление: **2026-10-11**  
Устройство: **Xiaomi Redmi 10C `fog`/`rain`**, Qualcomm **SM6225/Khaje**.  
Загрузка: постоянно со **слота B**; Android сохранён на слоте A; rootfs — Ubuntu 26.04 на разделе `cust` (сборка — `rootfs/`).

## Сводка: работает / в разработке / не работает

### Работает (проверено на устройстве)

| Компонент | Что проверено |
|---|---|
| Загрузка Linux со слота B | `systemd` `running`, 0 failed, ~39 с (kernel 10.7 + userspace 28.1) |
| Android на слоте A | не тронут, переключение слотов работает |
| Экран | DSI-панель Xinli FT8006S (`card0-DSI-1`, 720×1650@60, `connected`), апстрим `msm_dpu`/`msm_dsi`/`msm_dsi_phy` + наш `panel-xinli-ft8006s` |
| USB | gadget ADB + ACM (консоль `/dev/ttyACM0`), UDC `4e00000.usb` |
| Хранилище | UFS (`sda`, корень `sda8`); **SD-карта**: ro/rw, запись 8.6–10 МБ/с, чтение 77 МБ/с |
| Wi-Fi | WCN3990 / ath10k, `wlan0`, ping 0%; board-2 собран из стокового `bd3qvdfu.bin` («invalid board magic» убран) |
| Bluetooth | hci0 (QCA UART, `cmbtfw13.tlv`/`cmnv13t.bin`), контроллер powered |
| **Bluetooth-аудио (A2DP)** | уши EW72: `bluez_sink…a2dp_sink` RUNNING, кодек SBC, музыка играет |
| **Аудио (динамик/провод)** | ASoC-карта `0 [XiaomiRedmi10C]: qcm6490`; `aplay`/`speaker-test` играют |
| Тачскрин | FocalTech FT8006S (SPI), multitouch (34 касания, 2 потока) |
| **Кнопки** | Power (выключение), Громкость ↑ (`gpio-keys`), ↓ (`pm8941_resin`) — 172 нажатия |
| **Сенсоры (SSC)** | `hexagonrpcd` (2 юнита) + `sensors-setup`: акселерометр X=9.8 м/с², свет ~69 Lux, proximity `FAR` через `ssccli` |
| **GPS / GNSS (стек)** | движок в модеме (QMI LOC/PDS); MM Location + `gnss-share` отдают NMEA; `rain-loc-tool` применяет A-GPS-бандл (UTC-время, SUPL, MSB, XTRA-орбиты 168 ч); `gnss-keepalive` держит приёмник. **3GPP-локация (сота) работает** |
| Сотовый интернет | IPA v4.2 + модем + QMAP: LTE Tele2 RU, `qmapmux0.0`, ping 0% |
| Модем | регистрация, QMI/UIM, IMEI, ModemManager (`qcom-soc`), автовыбор carrier-конфига из PDC |
| **Hot-swap SIM** | вынул/вставил в слоте 1 на ходу → `uim-hotplug` перепривязывает подписку, LTE поднимается без ребута |
| Сеть | NetworkManager: LTE (`tele2`) + Wi-Fi одновременно, `nmtui`/`nmcli`; SSH (host-keys генерятся в сборке) |
| Питание | SMB1351 (зарядка), fuel gauge (`Charging`, SOC, V, I) |
| Thermal | TSENS (16 датчиков) + 4 cooling devices; **троттлинга нет** под 8-ядерной нагрузкой (плато ~59 °C, trip 95/125 °C) |
| CPU | cpufreq-hw + schedutil (A53 1.9008 / A73 2.4 ГГц), cpuidle |
| **Suspend / resume** | `s2idle`, сон/пробуждение кнопкой Power (см. ограничения) |
| GPU | Adreno 610: DRM/SMMU/GMU/ZAP, devfreq, Zonda PLL |
| Мелочи | вибратор (FF_RUMBLE), фонарик `white:torch`, RTC (1970 by design + chrony), подсветка |

### В разработке (частично)

| Компонент | Ограничение |
|---|---|
| **GPS: фикс координат** | Движок не захватывает спутники (`$GPGSA` пустой, `$GPGSV` без PRN) — условия приёма или созвездия (`Set GNSS Constellation` в libqmi нет). Решающий тест — GPS в Android на том же месте |
| Сенсоры: desktop-интеграция | Данные есть (`ssccli`), но `iio-sensor-proxy` не поднят (в Ubuntu нет пакета с libssc); автоповорот без него не работает. Оси акселерометра повёрнуты (фирмварь отдаёт нулевую матрицу) |
| Магнитометр / компас | В железе Redmi 10C их нет (проверено: данных нет) |
| Audio calibration (ACDB) | Вендорная калибровка не перенесена: ВЧ-шум аналоговой полки кодека |
| **Data в слоте 2 SIM** | Регистрация/голос идут, data модем отвергает (`cm error: no-service`); нужна DSD-подписка, которую ставит только RIL Android. Слот 2 погашен (`uim-provision`) |
| Suspend при включённом BT | Блокируется драйвером: `hci_uart_qca qca_suspend → -110`. С выключенным BT сон уходит штатно |
| ADB после resume | USB-гаджет не возвращается (`adb offline`) до ребута |
| ModemManager | Падает с SIGSEGV на неудачных connect (quirk 1.25.95); смягчено перезапуском |
| Голос/SMS через модем | Не тестировалось |
| HFP (звонки) через BT-гарнитуру | Не тестировалось (A2DP проверен) |

### Не работает

| Компонент | Причина |
|---|---|
| Камера (сенсор) | CAMSS/TFE + CCI подняты (`/dev/media0`, `vfe0_video0`, CCI `i2c-0/1`); S5KJN1 находится на CCI `0x36`, но probe падает: MCLK-клок (PLL) + питание рельсов (PM8008 не отвечает) |
| Сканер отпечатков | Драйвер отсутствует |
| NFC / ИК-порт | Нет в устройстве |

## Сессия 2026-10-11: что сделано

| Тема | Итог |
|---|---|
| **Сенсоры + аудио** | Корневая причина найдена: `rain-stable-boot.sh` глушил **все** remoteproc'и, включая ADSP (remoteproc1) — модем потом поднимался, а ADSP нет. Скрипт теперь глушит только модем. Следствия: заработали сенсоры (`/dev/fastrpc-adsp`, hexagonrpcd) и ASoC-карта (не было `no soundcards`) |
| **Ядро** | `q6dsp-lpass-clocks.c`: guard `q6dsp_cc_get()` против NULL-deref на PDR-индикации ADSP (в `kernel.patch`, прошито) |
| **BT-аудио** | `--disallow-module-loading` в `pulseaudio.service` мешал динамической загрузке `module-bluez5-device`; убран + ликвидирован второй инстанс PA |
| **GPS** | `rain-loc-tool` (libqmi напрямую) воспроизводит бандл вендорского HAL: UTC-время, SUPL, режим MSB, **XTRA-орбиты**; `gnss-keepalive` держит приёмник. MM этого не может (ждёт индикаций) |
| **Слот 2 / hotplug** | `uim-hotplug` перепривязывает подписку на ходу; слот 2 погашен как неподдерживаемый |
| **Воспроизводимость** | Каталог `rootfs/` в репо: `build-rootfs.sh`, `pivot-init`, `tools/rain-overlay` (юниты, netplan, скрипты), `tools/rain-loc-tool.c`. Релизный тарбол теперь несёт `sensors/` (hexagonrpcd + musl) |
| Стоковые данные | Распакован `super.img` (LP-метаданные) → `vendor` (ext4); GNSS-конфиги в `/mnt/128/stock-fw/gnss/` |

## Сводная таблица (детально, история)

| Компонент | Статус | Что именно проверено / ограничение |
|---|---|---|
| Загрузка rescue-ядра | **Работает** | Linux 7.1.5 загружается на `fog`; USB-консоль доступна |
| Android fallback | **Работает** | Android на слоте A сохранён; rescue грузится временно |
| Экран / simplefb-консоль | **Работает** | Вывод rescue-консоли через текущий framebuffer |
| DRM / MDSS / DPU / DSI-драйверы | **Работают** | Драйверы собираются и регистрируются; DPU и DRM-карта поднимаются |
| DSI-панель / panel pipeline | **Работает** | Xinli FT8006S: `card0-DSI-1`, `status=connected`, режим `720x1650`; panel driver, DSI PHY и MDSS/DPU проверены на `fog` |
| USB gadget ACM | **Работает** | Хост видит `/dev/ttyACM0`, доступен shell |
| UFS | **Работает** | Контроллер и накопитель обнаруживаются |
| SD-карта | **Работает** | Контроллер/карта обнаруживаются |
| Touchscreen FocalTech FT8006S | **Работает** | SPI-драйвер и firmware из Android; multitouch events проверены |
| Thermal sensors | **Работает** | Температурные датчики регистрируются |
| Vibrator | **Работает** | `gpio-vibrator` (input0, FF_RUMBLE); румбл проверен на устройстве |
| Фонарик | **Работает** | LED `white:torch`; вкл/выкл проверены |
| Зарядка / fuel gauge | **Работает** | Драйверы зарядки и измерения батареи поднимаются |
| SoundWire RX | **Работает** | Khaje frame/порт-параметры и RX-маршрут стабильны |
| Speaker audio | **Работает** | `audio-bind` сервис привязывает `a7c0000.pinctrl` после подъёма ADSP (гонка за clock `q6afecc`), применяет вендорный маршрут (RX2 → aw87xxx); карта `card0`, sink `speaker` |
| Audio calibration (ACDB) | **Не реализовано** | Android ACDB/userspace calibration ещё не перенесены |
| RX mute sequencing | **Частично проверено** | Vendor-подобная mute/unmute логика добавлена; тестировать только пустыми файлами |
| Wi-Fi | **Работает** | WCN3990/ath10k, подключение к сети проверено |
| Bluetooth | **Работает** | hci0 QCA UART (cmbtfw13.tlv/cmnv13t.bin); наушники подключены, A2DP-аудио через VLC проверено |
| Adreno 610 kernel init | **Работает** | DRM, SMMU, GMU, ZAP и GPU hw init проходят |
| GPU clock / PLL | **Работает** | Khaje ZONDA PLL0 → OUT_MAIN; вендорные 320/465/600/785/1025/1114.8 МГц доступны |
| GPU devfreq / OPP | **Работает** | `simple_ondemand`, `cur_freq`; таблица приведена к вендорной, предупреждение devfreq убрано |
| GPU real rendering/load | **Не проверено** | `kmscube`/Mesa/freedreno userspace-тест ещё не запускался |
| GPU userspace Vulkan/OpenGL | **Не проверено** | Kernel bring-up подтверждён, полноценный userspace stack не включён |
| Sensors (SSC / FastRPC) | **Работает** | Узел `qcom,fastrpc` (ADSP, SID 0x1c3-0x1c7) + `qcom,sm6225` в PD-mapper; `hexagonrpcd` + реестр из persist; `ssccli`: акселерометр, свет, приближение — живой поток |
| Автоповорот (Phosh) | **Работает** | udev `ACCEL_MOUNT_MATRIX` (поворот 180° вокруг Z) поверх SSC; Phosh claim'ит акселерометр |
| Модем (control plane) | **Работает** | ModemManager 1.25 находит модем по QRTR (`qcom-soc`), читает IMEI/прошивку MPSS; нужна SIM (сейчас `sim-missing`) |
| Модем (data / мобильный интернет) | **Не реализовано** | Требуется порт драйвера IPA для Khaje (см. ниже); без net-порта `modem-net` подставляет bridge-интерфейс |
| GNSS / GPS | **Частично** | QMI LOC/PDS (QRTR service 16) отвечает, `--loc-start` проходит; фикс требует обзора неба; `gnss-share` (mm-драйвер) поднят |
| Камера (CAMSS/TFE) | **Частично** | `camss` (3× TFE/CSID/CSIPHY, `qcom,sm6225-camss`) и `cci0` подняты: `/dev/media0`, `vfe0_video0`, `v4l-subdev0..17`, CCI `i2c-0/1`. Сенсор S5KJN1 детектится на CCI `0x36`, но не probe'ится (MCLK PLL + рельсы PM8008). Userspace-пайплайн (media-ctl/libcamera) не настроен |
| Audio microphone / recording | **Не завершено** | Полный capture path не подтверждён |

## Что означает статус

- **Работает** — проверено на реальном `fog` в rescue-образе.
- **Работает с дефектом** — основной путь функционирует, но известна проблема, указанная в таблице.
- **Не завершено** — часть kernel-side инфраструктуры может быть готова, но полный hardware/userspace цикл не подтверждён.
- **Не проверено** — намеренно не заявляется как рабочее до отдельного теста.
- **Не реализовано** — bring-up ещё не проводился.

## Рабочие артефакты

- `boot-GOOD8-gpu-full.img` — локальный образ с рабочим GPU kernel bring-up.
- `docs/GPU-BRINGUP.md` — подробности GPU.
- `docs/audio/NOISE-ANALYSIS.md` — расследование шума в аудио.
- `tools/fetch-firmware.sh` — загрузка закрытых firmware из release asset.

## Модем и GPS

Модем (MPSS) поднимается штатным `remoteproc` (`qcom/sm6225/modem.mdt`), после
чего по QRTR доступен полный набор сервисов (CTL, DMS, NAS/LTE, UIM, WMS,
Voice, IMS, **Location/PDS v2**, IPA control, Data Port Mapper). Проверено на
устройстве: `qmicli -d qrtr://0 --dms-get-ids` возвращает IMEI, прошивка
`MPSS.HA.1.1.c1-00027`, режим переводится в `online`.

**Control plane** — `ModemManager` 1.25 (плагин `qcom-soc`) находит модем по
QRTR и создаёт объект модема. Полноценной работы (регистрация/SMS) ждёт только
установки SIM: сейчас `card state: absent`, `state: failed, sim-missing`.

**Блокер data-пути.** ModemManager отказывается создавать объект модема без
net-порта (`Failed to find a net port in the QMI modem`). У Khaje data-путь —
это IPA, которого в mainline нет, поэтому rmnet-интерфейс не появляется.
Обход: сервис `modem-net` создаёт bridge-интерфейс `rmnet0` и udev-правило
помечает его `ID_MM_PHYSDEV_UID="qcom-soc"` + `ID_MM_DEVICE_PROCESS=1`
(иначе MM отбрасывает его как «virtual device»). Это даёт control plane;
мобильный интернет по-прежнему требует IPA.

**Порт IPA (для мобильного интернета, не реализовано).** Собраны данные:

- Вендорный DT (`vbdtbs/v00.dtb`): `qcom,ipa@0x5800000`, regs
  `0x5800000+0x34000` / `0x5804000+0x28000`, IRQ 257/259, `ipa-hw-ver = <0x10>`,
  SMMU-контексты `ipa_smmu_ap(0x140)` / `wlan(0x141)` / `uc(0x142)`,
  `qcom,ipa_fws` (PAS id 15), `qcom,rmnet-ipa3`.
- Апстрим IPA поддерживает `sm6350` (v4.7), `sc7180` (v4.2), `sc7280` (v4.11),
  `sm8350` (v4.9) и др., но **не** `sm6115`/`sm6225`. В апстримном `sm6115.dtsi`
  уже есть регионы памяти `pil_ipa_fw_mem`/`pil_ipa_gsi_mem` и `RPM_SMD_IPA_CLK`.
- Прошивка IPA (`ipa_fws.mbn` / часть вариантов использует имя `scuba_ipa_fws`)
  в `NON-HLOS.bin` (FAT16) и в vendor-образе **не найдена** — вероятно, грузится
  самим модемом либо лежит в другом разделе; требует уточнения.
- BAM-DMUX (более простой legacy data-путь) в вендорном DT отсутствует.

Оценка: порт IPA для Khaje — крупная задача (новый `ipa_data` с resource/endpoint
конфигом, таблицы регистров, SMMU, прошивка, отладка), требует вендорных исходников
IPA-драйвера или реверса конфигурации.

## GPS

GNSS-движок живёт в модеме и доступен через QMI LOC/PDS (QRTR service 16).
`qmicli --loc-start` / `--loc-set-nmea-types=gga` проходят, режим `standalone`;
позиция/NMEA пока не приходят — нужен обзор неба (тест в помещении не показателен).
Userspace: `gnss-share` с `device_driver="mm"` читает NMEA из Location-интерфейса
ModemManager (`gps-nmea`/`agps` capabilities видны) и публикует их для geoclue.

## Телефония: UI и поведение SIM

- **Приложения:** `calls` (GNOME Calls, звонки) и `chatty` (SMS) — ставятся из
  pmOS, работают через ModemManager по D-Bus. Phosh 0.57 имеет модемные
  индикаторы (`gmobile`) и виджет мобильных данных.
- **Запуск MM:** OpenRC стартует `modemmanager` (владелец D-Bus-имени
  `org.freedesktop.ModemManager1` — `openrc.modemmanager`); модем появляется
  через ~40 с (MM ждёт, пока устоятся QRTR-сервисы).
- **Горячая замена SIM требует перезагрузки.** Если вынуть/вставить SIM при
  работающем модеме, UIM-подсистема залипает (`no-atr-received`, provisioning
  session не создаётся; `--uim-change-provisioning-session` → `Internal`,
  DMS-`reset` не помогает). После холодной загрузки сессия поднимается штатно
  для того слота, где стоит карта.
- **Слоты:** обе физические позиции работают (после загрузки `Primary GW:
  slot N`), но при горячей замене надёжнее ставить карту в основной слот.
- **Горячая замена SIM (продолжение).** Перебраны все способы ресета без
  перезагрузки: `--uim-reset`, `--uim-sim-power-off/on`,
  `--uim-change-provisioning-session`, DMS `reset`/`low-power`, полный
  перезапуск модема (driver unbind/bind + `echo start`) и форсированный краш
  модема (debugfs `crash`) — **ни один не восстанавливает сессию**. Полный
  цикл питания (`poweroff`, не `reboot`) сессию возвращает. Причина — питание
  SIM-слота (PMIC), которым управляет модем, а не AP; в вендорном DT нет ни
  SIM-LDO, ни card-detect для AP.
- **`sim-watchdog`** (rootfs-overlay): раз AP не может обесточить слот сам,
  сторож следит за состоянием карты по QMI (`--uim-get-slot-status`) и просит
  модем обесточить слот при извлечении (`--uim-sim-power-off`) и включить при
  вставке (`--uim-sim-power-on`), чтобы модем видел свежеподнятую карту.

## Слот A/B: смена из Linux НЕ поддерживается

Попытка реализовать переключение слота из Linux **отменена — подход оказался
багованным**. Как устроен слот (для справки, чтобы не повторять ошибку):

- Слот читает **ABL**. Он смотрит на **GPT**: бит `AB_PARTITION_ATTR_SLOT_ACTIVE`
  (0x04 в байте 54 каждой `_a`/`_b`-записи) и **TYPE GUID** разделов
  (`boot_ctl_set_active_slot_for_partitions()` в `hardware/qcom/bootctrl`).
- Дополнительно для UFS есть **boot LUN** (`bBootLunEn`): LUNA = слот a,
  LUNB = слот b; виден в sysfs
  `/sys/devices/platform/soc/4804000.ufshc/attributes/boot_lun_enabled`.

Почему нельзя менять по частям: QTI пишет **всё вместе** (GUID-ы + биты + boot
LUN). Если поменять только boot LUN или только биты, состояние становится
рассогласованным, **AVB перестаёт находить разделы и загрузка падает** (лечится
прошивкой стоковой GPT: `fastboot flash partition:4 gpt_both4.bin` из fastboot-ROMA,
либо `fastboot set_active`).

**Итог:** единственный надёжный путь смены слота — `fastboot set_active a|b`.
Инструмент `rain-slot` и его сборка удалены.

## Часы и RTC

При загрузке системные часы вставали в **1970** (`RTC_HCTOSYS` читает PMIC RTC,
а тот отдаёт сырой счётчик), и только chrony поправлял их через ~20–30 с.

Разбор:
- Драйвер `rtc-pm8xxx`, узел `1c40000.spmi:pmic@0:rtc@6000`.
- **Прямая запись RTC запрещена аппаратно**: SPMI-арбитр отвечает
  `disallowed SPMI write to sid=0, addr=0x6046` — регистр принадлежит другому
  execution environment (`write_ee != AP`), поэтому и `hwclock -w`, и chrony
  `rtcsync` не работают.
- Альтернативный путь драйвера — хранить смещение в **nvmem-ячейке** (PMIC
  SDAM) или **UEFI-переменных**. У khaje нет ни того, ни другого: SDAM
  (`0xb600`) занят fuel gauge, а EFI-runtime нет, т.к. загрузка идёт
  `fastboot boot` (не через EFI).
- Свойство `allow-set-time` в DT **не помогает** (запись всё равно отклоняется
  арбитром) — поэтому в DT его нет.

Решение — userspace, `rtc-clock` (rootfs-overlay): **счётчик RTC продолжает
идти и при выключенном телефоне**, поэтому сервис сохраняет пару
`(epoch, rtc_counter)` в `/var/lib/rtc-clock` и при загрузке восстанавливает
`epoch + (counter_now − counter_saved)`. Сервис в **boot**-runlevel
(`after hwclock`, `before chronyd`), период сохранения — 5 мин (после того, как
chrony уже поправил часы). Итог: **часы верны сразу при загрузке**.

## Обслуживание: модули должны совпадать с ядром

Boot-образ несёт только ядро, а модули лежат в rootfs. При тестах через
`fastboot boot` rootfs (раздел `cust`) не перезаливается, поэтому модули
**устаревают**: старый `fastrpc.ko` (сборка от 26 сен) всё ещё печатал
удалённые отладочные строки `DBGATTACH` ~3 раза в секунду (2608 строк в dmesg).
После синхронизации — 0.

Для этого добавлен `postmarketos/scripts/push-modules.sh`: пакует
`out/rootfs/lib/modules/<krel>` и устанавливает на устройство (`DEVICE_HOST` из
`config.env`).

## Загрузка и сжатие

Ядро в `boot.img` **распаковывает загрузчик (Qualcomm ABL, UEFI-based,
gzip/`miniz`)**, а **initramfs распаковывает само ядро** (`lib/decompress.c`).
У arm64 нет самораспаковывающегося `Image` (`arch/arm64/boot/compressed/`
отсутствует, `CONFIG_EFI_ZBOOT` выключен), поэтому:

| Компонент | Кто распаковывает | Поддерживаемые форматы |
|---|---|---|
| `kernel` | ABL | gzip (`Image.gz`); raw `Image` — без распаковки |
| `ramdisk` (initramfs) | ядро | gzip, bzip2, lzma, **xz**, lzo, **lz4**, **zstd** (по `CONFIG_DECOMPRESS_*`) |
| `dtb` | никто | raw |

Замеры (100 МиБ cpio, хост):

| Формат | Размер | Распаковка |
|---|---|---|
| `xz -9e` | 40.9 МиБ | **4.53 с** |
| `zstd -19` | 43.6 МиБ | **0.165 с** |
| `lz4 -12` | 54.5 МиБ | 0.19 с |
| `zstd -3` | 49.8 МиБ | ~0.17 с |
| `gzip -6` | 51.5 МиБ | ~1 с |

Выбран **`zstd -19`**: почти как xz по размеру, но ~27x быстрее распаковка.
`lz4` быстрее, но крупнее и не влезает в лимит `fastboot boot` (~57.5 МиБ:
boot-раздел 128 МиБ, но буфер загрузчика меньше).

**Старт-ап сервисов.** `pivot-init` теперь печатает метку времени
(`[+Ns]`) на каждой строке. Разбор dmesg показал провал ~21 с между монтированием
persist и подъёмом аудиокодека: `hexagonrpcd-adsp-sensorspd` в `start_post`
ждёт первое измерение датчика (до 6×4 с), а `audio-bind` и `modem-net` были
`after hexagonrpcd` — хотя датчики им не нужны. Зависимость убрана
(`audio-bind` → `after sensors-setup`, `modem-net` — только `before
modemmanager`), теперь аудио и модем не ждут сенсоров.

**ModemManager и polkit**: пакетные D-Bus-файлы содержали реальные `Exec`
(`/usr/sbin/ModemManager`, `/usr/lib/polkit-1/polkitd`), из-за чего D-Bus
поднимал демонов параллельно с OpenRC; экземпляр OpenRC проигрывал гонку за
D-Bus-имя, и сервисы показывали `stopped`/`failed`. Overlay переопределяет оба
файла на `Exec=/bin/false`, владельцем остаётся OpenRC — теперь `rc-status`
не показывает ни одного `failed`.

**Итог по загрузке: 34 с до сети** (было ~60–65 с): ~10–14 с дало zstd,
~16–20 с — снятие ложных зависимостей. Образ 50.81 МиБ.

## Камера S5KJN1: clock-разбор (2026-10-12, ветка kernel)

Разобраны два «hard lockup»-источника на пути STREAMON и доведён pipeline до
стрима без падений. Кадров пока нет.

**1. Lockup №1 — propagation в живой GPLL0.** Камерные RCG в
`drivers/clk/qcom/gcc-sm6115.c` шли с `clk_rcg2_shared_ops` + `CLK_SET_RATE_PARENT`.
`clk_set_rate(csiphy0, 240M)` поднимался до `gpll0` и перепрограммировал главный
PLL системы на ходу (в логе: `CLKDBG ... calc done top=gpll0` → смерть всех CPU).
Ремонт: `CLK_SET_RATE_PARENT` с камерных RCG снят, у них оставлен
`CLK_OPS_PARENT_ENABLE` (как в стоковом `gcc-khaje.c`).

**2. Lockup №2 / «tfe_0_clk stuck at 'off'» — неверный тип PLL.** Аппаратура
Khaje использует layout **LUCID (== TRION)**, а драйвер описывал GCC PLL как
`CLK_ALPHA_PLL_TYPE_DEFAULT_EVO` (bengal). Из-за сдвига офсетов (USER_CTL 0x0c
vs 0x18, CONFIG_CTL 0x18 vs 0x20, ALPHA 0x40) `gpll10` оставался залочен, но
**его выход не был включён** → мультиплексор RCG на мёртвом источнике →
`CMD_UPDATE` не завершался → ветка TFE «stuck at off» (-16).
Ремонт для gpll10: `.regs = CLK_ALPHA_PLL_TYPE_LUCID`, `.ops = clk_alpha_pll_ops`,
`CLK_IS_CRITICAL`, VCO `lucid_vco`, конфиг из стокового `gcc-khaje.c`
(`l=0x3C, config_ctl=0x20485699, config_ctl_hi=0x2261, config_ctl_hi1=0x329A299C,
user_ctl=0x1, user_ctl_hi=0x805`), `clk_alpha_pll_configure()` (не trion-вариант —
тот гасит PLL). После этого `gpll10` = 1152 МГц, enabled, а `gpll10_out_main`
читается как 1152 МГц (постдив /1).

**3. Инструментация bisect.** `CAMSS_ABORT(dev,n)` в `camss.h` делает голый
`return -EIO` — в `vfe_get()` это оставляет `vfe->power_lock` захваченным и все
последующие тесты виснут на мутексe (выглядит как «hard lockup», на самом деле
D-состояние). Для bisect уровень ставить только там, где abort не внутри
залоченной секции, либо починить макрос на `goto`.

**Текущее состояние.** `STREAMON` проходит полностью: sensor → csiphy0 → csid0 →
vfe0_rdi0, сенсор реально стримит (0x0100=1 через CCI), CSID RDI0 = RAW10/VC0/4
линии/PHY0 включён, CSIPHY lane-регистры совпадают с qcm2290-таблицей, система
не падает. Кадров нет: IRQ CSIPHY/CSID/VFE по данным не приходят, `DQBUF` →
poll timeout. Дальше искать, почему CSI-2 не доходит от сенсора до PHY
(сверить с Android: lane-assign/combo-mode/data-rate из модуля
`com.qti.sensormodule.c3q_sunny_s5kjn1.bin`, либо снять дамп регистров CSIPHY/CSID
на стоке при работающей камере).

### Продолжение (2026-10-12, вечер): сенсор стримит, PHY видит данные

**3. Постдивы PLL — тот же layout-баг.** `gpll0_out_aux2` (родитель
`csiphy0_timer`) читался как 74.4 МГц вместо 297.6 — постдивы остались на
`DEFAULT_EVO`. После перевода всех `clk_alpha_pll_postdiv` на LUCID:
`gpll0_out_aux2` = 297.6 МГц, `gpll10_out_main` = 1152 МГц, timer = 297.6 МГц.

**4. PLL сенсора S5KJN1 зависит от MCLK.** В драйвере стояли 24-МГц значения
(`0x0136=0x1800, 0x013e=0x0000, 0x0304=0x0004, 0x0306=0x008c, 0x030e=0x0004,
0x0310=0x0092`), а MCLK у нас 19.2 МГц (совпадает со стоковым DTB:
`clock-rates = <0x124f800>`). Взяты 19.2-МГц значения из upstream-патча
(Danila Tikhonov, «s5kjn1: Add support for 19.2MHz MCLK»):
`0x0136=0x1300, 0x013e=0x00c8, 0x0304=0x0003, 0x0306=0x0083, 0x030e=0x0003,
0x0310=0x0089` (и `0x0310=0x006d` для 8160x6144). Остальные регистры обеих
mode-последовательностей совпадают с upstream (проверено diff'ом).

**5. Что проверено и работает.** `STREAMON` проходит; сенсор реально стримит
(`0x0100=1` по CCI); PLL-регистры сенсора держат 19.2-МГц значения; CSIPHY
(3ph, layout qcm2290) сконфигурирован — lane-регистры `0x0=0xD7, 0x1c=0x0A,
0x2c=0x01, 0x30=0x02`, CTRL5=0xD5 (clk + lanes 0..3), CTRL6=0x1, CTRL7=0x2;
CSID RDI0 = `0x802BF000` (DT=0x2B RAW10, VC=0, ENABLE), `CSI2_RX_CFG0 =
0x00132103` (4 линии, lane_assign 0x3210, PHY0, D-PHY); все камерные клоки
(238/297.6/192/153.6 МГц) включены.

**6. Главный факт A/B.** Статусные регистры CSIPHY (`0x8b0/0x8b8/0x8c4`)
меняются с 0 на `0x4/0x80/0x10` **только когда сенсор стримит** (проверено
через `skip_stream`). То есть сенсор передаёт, и PHY это видит. Но IRQ
CSID0/VFE0 по данным не приходят и `DQBUF` не возвращается — данные не
проходят PHY→CSID→VFE.

**Дальше искать**: связку CSIPHY→CSID (кодирование `PHY_NUM_SEL`/DLX для этой
ревизии, доп. регистры CSIPHY 3ph, settle/timing) — сверять со стоковым
Android (дамп CSIPHY/CSID при работающей камере) либо с downstream-исходниками
Khaje. Инструментация `CAMSS_ABORT` для bisect требует починки (голый
`return` оставляет mutex захваченным).

## КАМЕРА S5KJN1 РАБОТАЕТ (2026-10-12, 20:00)

Кадры пошли: `capN 6 8` даёт 8 кадров подряд (seq 0..7), по 15679488 байт
(4080x3072 RAW10 packed). Первый кадр сохранён: `out/backups/first-frame-s5kjn1.png`.

### Что чинилось (по порядку)

1. **`CLK_SET_RATE_PARENT` на камерных RCG** → `clk_set_rate(csiphy0)` уходил вверх
   и перепрограммировал живой `gpll0` → hard lockup всех CPU. Убрано (как в стоке).

2. **Тип GCC-PLL.** Khaje использует layout **LUCID (== TRION)**, драйвер описывал
   PLL как `DEFAULT_EVO` (bengal) → `gpll10` был залочен, но выход не включён →
   мультиплексор RCG на мёртвом источнике → `CMD_UPDATE` не завершался →
   `tfe_0_clk status stuck at 'off'`. Переведено: `gpll10` → LUCID + конфиг из
   стокового `gcc-khaje.c`; все `clk_alpha_pll_postdiv` → LUCID
   (это же починило `gpll0_out_aux2`: было 74.4 МГц, стало 297.6 МГц — родитель
   `csiphy0_timer`).

3. **PLL сенсора зависит от MCLK.** В драйвере стояли 24-МГц значения, MCLK 19.2 МГц
   (совпадает со стоковым DTB). Взяты 19.2-МГц значения из upstream-патча:
   `0x0136=0x1300, 0x013e=0x00c8, 0x0304=0x0003, 0x0306=0x0083, 0x030e=0x0003,
   0x0310=0x0089` (и `0x0310=0x006d` для 8160x6144).

4. **CSIPHY — чужая таблица.** В стоке `qcom,csiphy-v1.2.1`; mainline для SM6225
   писал таблицу `lane_regs_qcm2290`. Добавлена `lane_regs_sm6225` (копия
   mainline-таблицы 1.2.1 со `0x?14 = 0x80`, как в стоке), выбор по
   `qcom,sm6225-camss`. **После этого пошли данные:** CSID увидел SOT/EOT на всех
   4 линиях.

5. **Клоки CSID/TFE были в 2-3 раза ниже стоковых** (192/153.6 МГц вместо
   426.4/460.8 МГц) → FIFO всех линий CSID переполнялись, шли CRC/ECC.
   Заданы стоковые: `tfe_0_csid_clk = 426.4 МГц` (GPLL3/2.5), `tfe_0_clk = 460.8 МГц`.

6. **CSID RDI:** добавлены `RDI_CFG1 = 2` (TIMESTAMP_STB_POST_IRQ) и
   `RDI_ERR_RECOVERY_CFG0 = 0x9` (детект ошибок/переполнения) — как в стоке.

7. **TFE write-master:** добавлен `bw_limit = 0xf` (в стоке пишется, в mainline нет).
   Это убрало `VFE0: Input data violation`.

8. **ГЛАВНОЕ: камерный AXI-клок.** Сток (узел `cam-cpas`) задаёт
   `gcc_camss_axi_clk` = 80/150/240/**300 МГц** по уровням. У нас он оставался
   **19.2 МГц** (парковка на TCXO), потому что `vfe_match_clock_names()` не
   пропускал имя `"axi"` — частота вообще не задавалась. Шина записи в память
   не успевала → `RDI0_OVERFLOW`. Добавлено `"axi"` в matcher + частота
   `300000000` в `vfe_res_sm6225`. **После этого пошли кадры.**

### Что осталось

- Цвета/байеровский паттерн проверить (`SGRBG10P` — подтверждён GRBG численно,
  остался вопрос цветокоррекции в userspace).
- Проверить второй режим (8160x6144) и переднюю камеру (Hi556, CCI1).
- Автоэкспозиция: сейчас фикс. exposure/gain через `/dev/v4l-subdev18`
  (напр. `exposure=4266, analogue_gain=32` → картинка как в Android-референсе).

## Аудит/чистка камерных патчей (2026-10-12, после рабочего стрима)

Сняли «лишнее» по одному изменению за раз, после каждого — сборка +
`fastboot boot` на слот B + `capN 6 3` (3 кадра, 15679488 байт, без
violation/overflow в dmesg). Сравнение шло с upstream v7.1.5
(собственной поддержки sm6225 там нет — ресурсы сверялись со стоковым
`gcc-khaje`/DTB).

### Фаза 1 — убрана отладка (не влияла на функцию)

`CLKDBG` (clk.c), `RCG2DBG` (clk-rcg2.c), `BRINGUP gpll9/gpll10`
force-enable (gcc-sm6115.c), вся `CAMSS_ABORT`-машинерия + маркеры
(camss*.c, camss.h), `skip_stream` и no-op `power_off`, i2c-sweep и
отладочные `dev_err` (s5kjn1.c), ICC-логирование в runtime_suspend/resume
(camss.c). После чистки стрим работает.

### Фаза 2 — A/B-тест «настоящих» костылей

| Изменение | Результат | Вердикт |
|---|---|---|
| `"axi"` в `vfe_match_clock_names` + rate в `vfe_res` | Убрано; частота перенесена в DTS: `assigned-clocks = <&gcc GCC_CAMSS_AXI_CLK>; assigned-clock-rates = <300000000>` на узле `camss`. AXI=297.6 МГц ещё до стрима, кадры идут | **Заменено DT-способом, `camss-vfe.c` теперь upstream-identical** |
| `CSID_RDI_CFG1=0x2` (timestamp strobe) | Убрано — стрим идёт без него | **Не нужно, удалено** |
| `CSID_RDI_ERR_RECOVERY_CFG0=0x9` | Убрано — стрим идёт без него | **Не нужно, удалено** (обнаружение overflow работает и так — видимо, включено по умолчанию/через другие биты) |
| `TFE_BUS_BW_LIMIT=0xf` | Убрано — стрим идёт без него | **Не нужно, удалено.** Раньше «чинило» `Input data violation` только в связке с остальным; реальная причина была в клоках CSID/TFE/AXI |
| BRAMMO enable-бит в `clk_alpha_pll_enable` | Убрано — стрим идёт | **Не нужно при LUCID-ops** — `clk_alpha_pll_configure` из probe и on-demand enable покрывают путь |
| `CLK_SET_RATE_PARENT` на 18 camss-RCG | Вернул апстримный флаг → **hard lockup при STREAMON** (capN завис, система встала) | **Снятие флага ОБЯЗАТЕЛЬНО.** Пропагация `clk_set_rate` уходит в живой PLL (gpll0/gpll10-chain). Альтернатива на будущее: сделать разделяемые PLL fixed-ops, как у downstream, — тогда флаг можно вернуть |
| `lane_regs_sm6225` (100 строк) | Дедуп с upstream: отличается от `lane_regs_sm8250` (та же 1.2.1 3ph) только в `0x?14`: `0x80` vs `0x60` на всех 5 линиях | **Оставлена целиком** — 100-строчная таблица на SoC — нормальная практика этого файла; ради 1 регистра усложнять механизм записи не стоит |
| `clk.c` `reparent_orphans` (без before/after dance) | Вне камеры (DSI PHY PLL WARN'и на SM6225) | **Оставлено**, отдельный A/B на дисплейном пути |

### Итоговый остаток diff (камера)

- `camss.c` +217 — таблицы `*_res_sm6225` (3× CSIPHY/CSID/VFE, ICC, ops).
- `camss-csiphy-3ph-1-0.c` +111 — `lane_regs_sm6225` + выбор по compat.
- `s5kjn1.c` ±13 — MCLK 19.2 МГц + PLL-значения.
- `gcc-sm6115.c` — LUCID-PLL/postdiv, `gpll10_config`, снятые флаги
  `CLK_SET_RATE_PARENT` на camss-RCG, `CLK_IS_CRITICAL` на gpll10.
- `clk-alpha-pll.c` — фикс `zonda_pll_adjust_l_val` (баг бэкпорта:
  `quotient` вместо `rate` → PLL_L_VAL).
- `clk.c` — `reparent_orphans` workaround (дисплейный, не камерный).
- `sm6225.dtsi` — `assigned-clocks` для `GCC_CAMSS_AXI_CLK`.
- `camss-video.c`, `camss-vfe.c`, `camss-vfe-340.c`, `camss-csid.c`,
  `camss-csid-340.c`, `camss-csiphy.c`, `camss.h`, `clk-rcg2.c` —
  **полностью upstream-identical.**

Бэкап чистого образа: `out/backups/boot-linux-cleanup-audit.img`.
