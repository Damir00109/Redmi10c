# Статус bring-up

Последнее обновление: **2026-09-27**  
Устройство для проверки: **Xiaomi Redmi 10C `fog`**, Qualcomm **SM6225/Khaje**.  
Метод загрузки: временный `fastboot boot`, без постоянной прошивки разделов.

## Сводная таблица

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
| Камера | **Не реализовано** | Драйверы и pipeline не поднимались |
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
