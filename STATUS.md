# Статус bring-up

Последнее обновление: **2026-10-20**  
Устройство: **Xiaomi Redmi 10C `fog`/`rain`**, Qualcomm **SM6225/Khaje**.  
Загрузка: постоянно со **слота B**; Android сохранён на слоте A; rootfs — Ubuntu 26.04 на разделе `cust` (сборка — `rootfs/`).

## Сводка: работает / в разработке / не работает

### Работает (проверено на устройстве)

| Компонент | Что проверено |
|---|---|
| Загрузка Linux со слота B | `systemd` `running`, 0 failed, ~39 с (kernel 10.7 + userspace 28.1) |
| Android на слоте A | не тронут, переключение слотов работает |
| Экран | DSI-панель Xinli FT8006S (`card0-DSI-1`, 720×1650@60, `connected`), апстрим `msm_dpu`/`msm_dsi`/`msm_dsi_phy` + наш `panel-xinli-ft8006s` |
| USB | gadget ADB + ACM (консоль `/dev/ttyACM0`), UDC `4e00000.usb`; **проверено на 3/64 и 4/64** (`snps,dis-64bit-dma` капает маску до 32 бит — см. AGENTS.md) |
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
| Питание | SMB1351 (зарядка), fuel gauge SH366101: SOC, V, I, temp, циклы, STATUS/HEALTH + расширенные блоки (Qmax→SOH, энергия мВт·ч, OCV, внутр. темпа кристалла) |
| Thermal | TSENS (16 датчиков) + 4 cooling devices; **троттлинга нет** под 8-ядерной нагрузкой (плато ~59 °C, trip 95/125 °C) |
| CPU | cpufreq-hw + schedutil (A53 1.9008 / A73 2.4 ГГц), cpuidle |
| **Suspend / resume** | `s2idle`, сон/пробуждение кнопкой Power (см. ограничения) |
| GPU | Adreno 610: DRM/SMMU/GMU/ZAP, devfreq, Zonda PLL |
| **GPIO / pinctrl** | Основной TLMM: **115 из 121** линий открыты в `gpiochip0`; в резерве только 14–17 (LPI/LPASS-остров, навсегда) и 66/67 (припаркованный CSI-mux). Полный свип всех пинов: чтение/запись CTL+IO, `INTR_CFG`, quiesce-последовательность — крашей нет. Карта: `docs/GPIO-PIN-MAP.md` |
| Мелочи | вибратор (FF_RUMBLE), фонарик `white:torch` + вспышка, RTC (1970 by design + chrony), подсветка |

### В разработке (частично)

| Компонент | Ограничение |
|---|---|
| **GPS: фикс координат** | Движок не захватывает спутники (`$GPGSA` пустой, `$GPGSV` без PRN) — условия приёма или созвездия (`Set GNSS Constellation` в libqmi нет). Решающий тест — GPS в Android на том же месте |
| Сенсоры: desktop-интеграция | Данные есть (`ssccli`), но `iio-sensor-proxy` не поднят (в Ubuntu нет пакета с libssc); автоповорот без него не работает. Оси акселерометра повёрнуты (фирмварь отдаёт нулевую матрицу) |
| Магнитометр / компас | В железе Redmi 10C их нет (проверено: данных нет) |
| Audio calibration (ACDB) | Вендорная калибровка не перенесена: ВЧ-шум аналоговой полки кодека |
| **Data в слоте 2 SIM** | Регистрация/голос идут, data модем отвергает (`cm error: no-service`); нужна DSD-подписка, которую ставит только RIL Android. Слот 2 погашен (`uim-provision`) |
| Suspend при включённом BT | **Исправлено (2026-10-18)**: `rain-bt-suspend.service` (Before=sleep.target) отвязывает serdev `serial0-0` перед сном, `rain-bt-resume.service` привязывает обратно. Проверено с включённым BT: сон состоялся (`suspend_stats=2`, 0 «Some devices failed»), `hci0` вернулся `UP RUNNING` |
| ADB после resume | **Исправлено (2026-10-18)**: `usb-adb-resume.service` перепривязывает гаджет (unbind → restart adbd → bind) — `adb` возвращается онлайн сам |
| ADSP после resume | **Исправлено (2026-10-18)**: причина оказалась не в питании и не в отсутствии уведомления о сне, а в **fastrpc-сессии**, которую `hexagonrpcd` держит через сон — сервис `frpc_dsp` на DSP падает сразу после пробуждения, remoteproc перезапускает весь ADSP, и от этого сыпались SoundWire/кодек. `rain-sensor-suspend.service` (Before=sleep.target) гасит `hexagonrpcd` до сна: ADSP переживает resume, шквал исчез (проверено: счётчик крахов не растёт, 0 строк SoundWire в окне resume) |
| Сенсоры после resume | **Исправлено (2026-10-18)**: краш DSP-процесса `sensor_process` рвёт fastrpc (`Broken pipe`) → оба `hexagonrpcd` деактивируются и сенсоры мертвы до ребута. `rain-sensor-resume.service` перезапускает их после пробуждения |
| I2C во время сна | **Исправлено (2026-10-18)**: опрос зарядника/гейджа шёл на незамораживаемой `system_wq` → `i2c-3: Transfer while suspended` WARNING. Оба драйвера переведены на `system_freezable_wq` |
| Сенсоры в приложения | **Сделано (2026-10-18)**: ядерных IIO у нас нет (сенсоры на шине ADSP), поэтому `rain-sensor-bridge.py` публикует акселерометр и ALS на system-bus в интерфейсе `net.hadess.SensorProxy` (тот, что даёт iio-sensor-proxy и ждут GNOME/Plasma/phosh): `AccelerometerOrientation`, `LightLevel`, `Claim*`. Проверено: свойства читаются, значения живые (свет 120 лк, ориентация считается) |
| **Рестарт ADSP** | **Исправлено (2026-10-20)**: рестарт ADSP ронял SoC — Oops в `clk_q6dsp_prepare` (drvdata публиковался после регистрации клоков, а регистрация запускает reparent-orphans → `prepare` на новом родителе). После этого сыпались `clk.c:1188/1047` WARN'ы из `clk_unregister` (enable/disable-танец при переселении в orphan, хотя мигрировать некуда). Оба места починены; проверено: два цикла stop/start ADSP подряд — ноль WARN/Oops, `remoteproc1: running`, звуковая карта жива, fastrpc-сервисы перезапускаются. Остаточное: SoundWire-кодек после рестарта DSP требует ре-инициализации (в окне рестарта `trf on Slave 1 failed`) |
| ModemManager | Падает с SIGSEGV на неудачных connect (quirk 1.25.95); смягчено перезапуском |
| **Камера depth OV02B1B** | **Разгадано (2026-10-19)**: у модуля два i2c-устройства — сенсор на `0x3c`, EEPROM 24c02 на `0x59`; DT указывал `0x59`, поэтому драйвер читал EEPROM (`unexpected sensor id(0x4233)` = ASCII «B3» из заголовка «B313») и STREAMON падал EPIPE/EINVAL. Плюс сенсор виден на CCI1 только при `gpio66=1` (CSI-mux OE, ранее припаркован). После правки (reg 0x3c, gpio-hog 66 high / 67 low, at24@0x59) ID-проверка проходит и пайплайн `csiphy1→csid1→vfe1_rdi0` стартует штатно, но **кадры не приходят** — остаётся разобраться с lane/PHY и уровнем SEL мультиплексора. Задняя и передняя камеры при этом работают |
| Голос/SMS через модем | Не тестировалось |
| HFP (звонки) через BT-гарнитуру | Не тестировалось (A2DP проверен) |

### Не работает

| Компонент | Причина |
|---|---|
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
| Зарядка / fuel gauge | **Работает** | SMB1351 + SH366101; базовые SBS-свойства и расширенные gaugeinfo-блоки (Qmax/SOH, энергия, OCV, внутр. темпа) — см. `docs/hardware-inventory.md` |
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
| Камера задняя S5KJN1 | **Работает** | Полный путь сенсор→CSIPHY0→CSID0→TFE0→память: кадры 4080x3072 RAW10 непрерывно (проверено сотнями кадров). Контролы exposure/gain/vblank/hblank доходят до регистров. WL2866D питает рельсы. Ограничения: нет AE/AWB/LSC/color matrix (задача ISP-стека, не драйвера), второй режим 8160x6144 не проверен |
| Камера передняя Hi556 | **Работает** | Сенсор на CCI1 (i2c-1) адрес 0x28 (не 0x40!), CSIPHY2→CSID2→VFE2_RDI0→/dev/video8. Кадры 2592x1944 GRBG10 RAW10. Питание: WL2866D DVDD1=1.2В/AVDD1=2.8В (enable 0x05), MCLK0 19.2МГц gpio20, reset gpio24. Фикс: upstream-драйвер был ACPI-only → добавлен of_match_table. Ограничения: нет AE/AWB (как у задней) |
| Камера EEPROM ×3 | **Работает** | Все три модульных EEPROM привязаны через at24 и читаются из sysfs: задний S5KJN1 `0-0051` 16КБ (24c128, Sunny: module info + AWB/LSC + AF-калибровка DAC для GT9764), передний Hi556 `1-0051` 8КБ (24c64, AAC "V6457"), depth OV02B1B `1-0059` 256Б (24c02, AAC "B313"). Содержимое совпало с raw-дампами побайтово. Бэкап: `/mnt/128/backups/camera-20261013-eeprom/` |
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

## Декод кадра: баг распаковки RAW10 (2026-10-12, вечер)

Вся «подозрительность» картинки (шум ~70%, «склейки», ложная яркость)
была багом хостового декодера, **не** камеры:

- Правильная распаковка `SGRBG10P` (MIPI RAW10 packed): 5 байт = 4 px,
  байты 0–3 — старшие 8 бит, байт 4 — младшие 2 бита каждого:
  `px_i = (byte_i << 2) | ((byte4 >> (2*i)) & 3)`.
- Было неправильно: `px_i = byte_i | (lsb2 << 8)` — младшие биты вдвигались
  в СТАРШИЕ разряды → мусор в MSB. Отсюда «шум» и завышенный mean.
- Плюс обязателен построчный разбор: stride буфера **5104**, полезных
  **5100** байт/строка (4 байта паддинга до 64 бит).
- С честным декодом на ровном участке: std ≈ 2.2 кодов — данные чистые.

Проверка регистров сенсора вживую (`echo on > .../power/control`, затем
i2c read на `/dev/i2c-0` адрес 0x10): во время стрима `0x0202` =
установленная экспозиция, `0x0204` = gain×32, `0x0340` (VTS) = 4288 —
контролы реально работают. `0x0200` = 0x0100 — это fine integration
(доли строки), НЕ длина кадра.

Реальная картина: сцена темнее, чем казалось; при `exposure=4266,
analogue_gain=64` кадр экспонирован нормально (mean ~484, детали чёткие:
перфорация, кабели, текстура стены). Зелёный/желтоватый оттенок и
виньетка — отсутствие color matrix/AWB/LSC (задача ISP-стека, не драйвера).

## РЕШЕНО: EPIPE при STREAMON = неверный fourcc (2026-10-13, вечер)

Симптом: `VIDIOC_STREAMON returned -1 (Broken pipe)`, в dmesg тишина (ни
`Failed to start media pipeline`, ни `failed to start streaming`).
Затрагивало **обе** камеры (заднюю и переднюю) → выглядело как поломка CAMSS.

### Причина

В ядре 7.1.5 четырёхбуквенные коды packed-Bayer **исправлены** относительно
старых версий (`include/uapi/linux/videodev2.h`):

| Определение | fourcc |
|---|---|
| `V4L2_PIX_FMT_SBGGR10P` | `'pBAA'` |
| `V4L2_PIX_FMT_SGBRG10P` | `'pGAA'` |
| `V4L2_PIX_FMT_SGRBG10P` | `'pgAA'` |
| `V4L2_PIX_FMT_SRGGB10P` | `'pRAA'` |

Сенсор S5KJN1 (и Hi556) отдают media-bus код `SGRBG10_1X10`, которому
соответствует **`V4L2_PIX_FMT_SGRBG10P = 'pgAA'`**. Мы же ставили `pBAA`
(= `SBGGR10P`, другой байеровский порядок). `video_check_format()` в
`camss-video.c` сравнивает pixelformat видео-ноды с форматом субдева и при
несовпадении возвращает `-EPIPE` — молча, до вызова `s_stream`.

Диагноз получен временным `dev_err` в `video_check_format()` + в цикле
`s_stream` (после отладки принты убраны):

```
CAMDBG check_format: pix[fmt=pBAA 4080x3072 planes=1 field=1] sd[fmt=pgAA 4080x3072 planes=1 field=1]
```

### Правильный рецепт захвата (проверено на #74)

Задняя (S5KJN1, `/dev/video0`):

```sh
FMT="SGRBG10_1X10/4080x3072"
media-ctl -d /dev/media0 -l '"msm_csiphy0":1 -> "msm_csid0":0 [1]'
media-ctl -d /dev/media0 -l '"msm_csid0":1 -> "msm_vfe0_rdi0":0 [1]'
for p in '"s5kjn1 0-0010":0' '"msm_csiphy0":0' '"msm_csiphy0":1' \
         '"msm_csid0":0' '"msm_csid0":1' '"msm_vfe0_rdi0":0' '"msm_vfe0_rdi0":1'; do
  media-ctl -d /dev/media0 --set-v4l2 "$p [fmt:$FMT]"
done
v4l2-ctl -d /dev/video0 --set-fmt-video=width=4080,height=3072,pixelformat=pgAA \
  --stream-mmap=4 --stream-count=10 --stream-to=/tmp/rear.raw
```

Передняя (Hi556, `/dev/video8`): то же, но `SGRBG10_1X10/2592x1944`,
`msm_csiphy2`→`msm_csid2`→`msm_vfe2_rdi0`, `width=2592,height=1944`.

Размер кадра: задняя 15679488 Б (stride 5104, полезных 5100), передняя
6298560 Б. Распаковка — 5 байт = 4 px (см. раздел выше).

## GT9764 (автофокус задней камеры): статус на 2026-10-13

Железо по данным Android: `afAvailableModes=[0 1 2 3 4]`,
`minimumFocusDistance=10cm`, AF-калибровка в EEPROM `0-0051`
(inf≈488–522, macro≈987). Стоковый XML модуля `c3q_sunny_s5kjn1`:
slave addr `0x18`, Fast I²C, VAF-only power-up.

Что сделано и проверено в Linux:

- узел `actuator@18` на `cci0_i2c0`, драйвер `dw9768` с compatible
  `giantec,gt9764` — **привязывается** (`0-0018`, name `gt9764`);
- `cam_vaf` (fixed regulator на TLMM gpio102) — `enabled`, 2800000 мкВ,
  пин `gpio102: out high func0 2mA pull down` (в стоке active — bias-disable);
- WL2866D запрограммирован (ID `0x64`), перебор `0x0e` ∈ {0x0f, 0x15, 0x1f}
  (5-й выход из стокового трейса не подтвердился: бит4 не читается обратно);
- частота CCI0 пробовалась 1 МГц и 400 кГц.

**Проблема:** устройство `0x18` не отвечает ACK ни на read, ни на write —
ни в покое, ни во время стрима сенсора (когда шина «горячая»: `0x10` и
`0x51` ACK'ают), ни при включённом VAF. Слепая запись DAC (reg 0x03/0x04)
изменений фокуса не дала, **но** тест-сцена была тёмной/плоской
(max 120 из 1023, diff кадров ≈ 0.5 кода) — тест неубедителен, не
доказательство отсутствия AF.

Гипотезы на следующий раз (по убыванию вероятности):

1. **Питание VAF реально не доходит**: gpio102 — ключ, а вход ключа может
   питаться от рельсы, которую Android включает (в диффе регуляторов при
   открытой камере менялись `pm6125_l5`, `pm6125_l14`, `pm6125_s5_level`,
   `gcc_camss_top_gdsc`). Нужно снять дифф регуляторов Android ещё раз и
   завести найденную рельсу в DT как supply для `cam_vaf`.
2. **Адрес отличается** от 0x18 в этой ревизии модуля (XML мог быть от
   другого варианта) — перебрать адреса слепой записью DAC с проверкой
   фокуса по кадрам.
3. Повторить DAC-тест на **текстурной освещённой сцене** (обязательно:
   текущий кадр — почти чёрный).

Ссылки: бэкап состояния — `/mnt/128/backups/camera-20261013-af-depth-start/`,
инструменты `eetool` (dump/w8/w16/rd16/scan) и `i2cack` (address-only ACK
probe) — в `/tmp`, исходники рядом.

## GT9764, часть 2 (2026-10-13, ночь): PM8008 проверен и исключён

Гипотеза «VAF питается от PM8008 L5» была проверена на железе — **не
подтвердилась**, и это важный отрицательный результат.

### Что нашли в стоке

Разобрал DTBO (`/mnt/931/.../images/dtbo.img` → 40 оверлеев, дампы в
`/mnt/128/backups/dtbo-stock-fog/`). В `__fixups__` оверлеев камерные
supply-метки резолвятся так:

| Supply | Метка | Узел |
|---|---|---|
| `cam_vaf` | `L5P` | `/soc/i2c@4a84000/qcom,pm8008@9/.../qcom,pm8008-l5@4400` |
| `cam_vio` | `L7P` | pm8008 l7 |
| rear vana/vdig | `L4P` / `L2P` | pm8008 l4 / l2 |
| front vana/vdig | `L6P` / `L1P` | pm8008 l6 / l1 |
| depth vana | `L3P` | pm8008 l3 |
| cam_clk | `gcc_camss_top_gdsc` | — |

VIN-рельсы PM8008 (сток): `vdd_l1_l2 = pm6125_s6` (1.35 В),
`vdd_l7 = pm6125_s7` (2.04 В). EN — gpio26 (pinctrl `pm8008_active`,
output-high, pull-up, 2 мА), он же `en-gpio` у WL2866D. IRQ — gpio25.

### Почему это тупик

1. В **базовом стоковом DTB** `qcom,pm8008@8` и `@9` — `status = "disable"`.
2. Применённый оверлей (`ovl_31`) своими фрагментами 25/26 (адресующими
   `pm8008_8`/`pm8008_9`) ставит **`status = "disabled"`**.
3. **Живая DT в Android** (`/proc/device-tree`) подтверждает: оба узла
   `disable`. Строк `pm8008` в `dmesg` Android нет вообще.
4. В живой DT у актуатора (`/proc/device-tree/soc/qcom,cci0/qcom,actuator0`)
   есть `regulator-names = "cam_vaf"` и `rgltr-min/max = 0x2ab980` (2.8 В),
   `gpios = <tlmm 0x66=102>`, `cci-master = 0`, `status = ok`, **но свойства
   `cam_vaf-supply` нет**. Android-драйвер при этом пишет:
   `qcom,actuator0: supply cam_vaf not found, using dummy regulator`.
   То есть в стоке VAF — это тоже только ключ gpio102 без модели источника.

### Что проверили в Linux (и что не сработало)

Добавили узел `pmic@8` (mainline `qcom,pm8008`) на `&i2c1` (= стоковый
i2c@4a84000, где и WL2866D), включили VIN `s6`/`s7` через
`regulator-always-on`:

- probe доходит до записи масок IRQ и падает: `Failed to set masks in
  0x916: -6` (NAK) → регуляторы не регистрируются;
- ACK-скан шины 2 (16-битные чтения и 0-байтные probe) — **0x8 и 0x9 молчат**
  при включённых VIN; при этом на той же шине отвечают 0x28 (wl2866d),
  0x3e, 0x55 (sh366101), 0x58 (aw87xxx);
- импульс EN (gpio26 low 50 мс → high, через временный unbind wl2866d)
  ничего не изменил;
- `pm8008_l*` в `/sys/class/regulator` не появляются.

Вывод: PM8008 на этом экземпляре не отвечает и в прошивке выключен —
как источник VAF он недоступен. Узел оставлен в DTS со
`status = "disabled"` (как в стоке) и подробным комментарием, чтобы
гипотезу не проверяли повторно.

### Итоговое состояние GT9764

- Узел `actuator@18` (compatible `giantec,gt9764`) + драйвер `dw9768`:
  привязывается, `0-0018`, name `gt9764`.
- `cam_vaf` = fixed regulator на gpio102, **enabled, 2800000 мкВ**
  (pinctrl-эквивалент стока: output-high, 2 мА; у нас bias pull-down
  вместо bias-disable — на ACK не влияет, пин всё равно ведётся в high).
- **Устройство 0x18 не отвечает ни на одно обращение** — ни read, ни write,
  ни address-only probe; ни в покое, ни при стриме сенсора (когда 0x10
  отвечает), ни на 400 кГц, ни на 1 МГц. Полный скан CCI0 (0x08–0x77):
  отвечают только 0x10 (сенсор, при стриме) и 0x51 (EEPROM).
- Слепая запись DAC (0x02=0x00, затем 0x03/0x04 = 0 и 1023) при
  выставленной экспозиции (exposure=4266, gain=64) и текстурном кадре
  (max 1023, lapabs ≈ 12): diff кадров — уровень шума
  (mean 3.5, изменённых пикселей >32 всего 0.022%), резкость не меняется
  (lapvar 243.5 → 244.5). Движения линзы нет.

Остаётся один вопрос, который решается только глазами: **работает ли
автофокус в самом Android** на этом экземпляре. Метаданные
(`afAvailableModes = [0..4]`, `minimumFocusDistance = 10 см`) статичны —
они из XML модуля и не доказывают наличие VCM. Программная проверка
(читать живой `android.lens.focusDistance` из `dumpsys media.camera`)
упирается в экран блокировки — камера в этом состоянии не конфигурируется.

Если AF в Android тоже не фокусируется — актуатора на этом экземпляре нет
(или не разведён по I²C), и GT9764 закрывается как «hardware absent».
Если фокусируется — разница между Android и Linux где-то в CCI/питании,
и копать надо в сторону сравнения трасс CCI0 (в Android CCI наружу как
i2c-dev не отдан, /dev/i2c-0/1 — это Geni-шины 2 и 3).

## GT9764, часть 3 (2026-10-14): AF в Android работает, Linux не видит чип

### Ключевое доказательство из Android

Снял живые значения из `dumpsys media.camera` (камера 0 открыта, приложение
Aperture), пользователь при этом менял фокус:

```
22:00:23 afState=PASSIVE_SCAN   focusDist=0.00500000   (∞)
22:00:24 afState=PASSIVE_SCAN   focusDist=10.00000000  (10 см)
22:00:25 afState=PASSIVE_FOCUSED focusDist=5.05050516  (20 см)
22:00:27 afState=PASSIVE_FOCUSED focusDist=13.33333302 (7.5 см)
22:00:30 afState=PASSIVE_FOCUSED focusDist=3.53356910  (28 см)
22:00:34 afState=PASSIVE_FOCUSED focusDist=10.00000000 (10 см)
```

В результате (`latest received frame`) `android.control.afMode =
CONTINUOUS_PICTURE` — то есть HAL действительно работает с актуатором, а не
через EDOF. Пользователь подтвердил визуально, что наведение работает.

### Что в Linux при этом

`0x18` на CCI0 отдаёт **NAK (-ENXIO, errno -6) на ЛЮБУЮ форму транзакции** —
0-, 1-, 2- и 3-байтная запись, чтение с 8- и 16-битным адресом
(`/tmp/i2cerr.c`). Полный ACK-скан CCI0 (0x08–0x77) находит только `0x10`
(сенсор, во время стрима) и `0x51` (EEPROM модуля). Других устройств на
шине нет.

Проверено и **исключено** (всё в Linux, при стриме сенсора и VAF-ключе
gpio102 = high):

| Гипотеза | Как проверяли | Результат |
|---|---|---|
| WL2866D питает модуль иначе | прочитал все 16 регистров в Android (камера открыта) и в Linux | **байт-в-байт совпадают**, включая `0x0e=0x0a` (задняя пара) |
| Нужен power-cycle модуля | `0x0e=0x00` → пауза → `0x0a` (как в Android) | не помогло |
| PM8008 L5 питает VAF | узел `pmic@8`, VIN `s6/s7` = always-on, EN-импульс | PM8008 не ACKает вообще; в стоке **disabled** |
| Частота/тайминги CCI | `scl_stretch_en=1` (в стоке так) + шина 100 кГц вместо 400 | не помогло |
| Полярность VAF-ключа | unbind `cam_vaf` + импульс gpio102 | регулятор не отдал пин; сток явно `output-high` = active-high |
| Лишний GPIO питания модуля | живая DT: `qcom,cam-sensor0` = только MCLK2(gpio27) + RESET0(gpio18) | нет |
| Второй CCI-контроллер | сток: один `qcom,cci0` @0x5c1b000, два мастера | нет |
| Ошибки CCI в Android | `dmesg` после открытия камеры и тапов | ошибок нет вообще |

Итог: питание модуля, шина и адресация в Linux **идентичны** Android, но чип
по 0x18 не отвечает. Остаётся один вопрос, который решается только глазами:
**реально ли двигается линза в Android**. Отчёт `focusDistance` в Android —
это *командованная* позиция от AF-алгоритма (CAF считает её сам), поэтому
меняющиеся цифры сами по себе движения линзы не доказывают. Если при
наведении на текст с 10–15 см картинка **не** становится резкой — актуатора
на этом экземпляре нет, и GT9764 закрывается как «hardware absent».

Экспериментальные правки (`scl_stretch_en`, `clock-frequency=100000`)
откачены, дерево вернулось к upstream-состоянию CCI.

## Сессия 2026-10-17: среда восстановлена, baseline-срез, план стабилизации

### Среда поднята с чистого клона

Полный цикл от `git clone` до устройства: `fetch-firmware` (блобы с
релиза `firmware-2026.09.25`, через curl — `gh release download` виснет
на финализации) → `build.sh` (linux v7.1.5 + kernel.patch, Image.gz,
busybox, tinyalsa, initramfs, `out/boot-display-console-rescue.img`
52.78 МиБ) → модули `7.1.5-dirty` (528 шт) → `rootfs/build-rootfs.sh`
(Ubuntu 26.04.1, `linux_rootfs.sparse.img` 1.6G) →
`rootfs/tools/pack-boot-linux.sh` (`boot-linux.img` 51.65 МиБ).

### Прошивка — как прошло

`fastboot flash cust` **сломан на хосте** (fastboot 34.0.5-debian,
детерминированный `Error reading sparse file` на чанке 2/3; файл валиден).
Обошли через Android слот A + Magisk: `adb push` + `su -c 'dd …of=/dev/block/by-name/…'`.
`boot_b`←sde33, `cust`←sda8; проверено magic'ами (`ANDROID!`, ext4 `53 ef`).
После прошивки rootfs был забит 100% — `IMG_SIZE_MB=1800` < реальных 19.1G
на sda8, расширено `resize2fs` онлайн.

### Матрица переходов — вся проверена

`reboot bootloader` из Ubuntu работает нативно (`qcom-pon` + `mode-*` в
`pm6125.dtsi`, ~6с до fastboot — раньше считалось отсутствующим).
Android→fastboot `sys.powerctl`, Ubuntu↔Android `qbootctl -s`,
rescue `fastboot boot`, USB ACM `/dev/ttyACM0`, ADB+Magisk в Android.
Кнопки не нужны — цикл тестирования полностью софтовый.

### Baseline-срез (эталон для регрессий)

`docs/baseline-2026-10-17.txt` + `docs/baseline-2026-10-17-dmesg.txt`,
скрипт `/tmp/collect-baseline.sh` — повторять после правок и `diff`.
Эталон: systemd `running`, 0 failed, **0 warn/err в dmesg**, 8 ядер
schedutil, GPU devfreq, DSI+backlight, fts_ts, Wi-Fi 5GHz −51dBm,
BT UP, hexagonrpcd×3, camss 33 entities, remoteproc×2 running,
UFS HS_GEAR3. Аномалии эталона (не регрессии): SIM физически
отсутствует (`sim-missing` — норма), pulseaudio только `auto_null`
(звук осознанно не трогаем), `earlycon` malformed option.

### Аудит kernel.patch (93 файла, +17.5k): к чему стремимся

Патч в целом цивильный — хаки прокомментированы причиной. Главный долг:

- **fts_spi** (~5800 строк вендор-кода): `/proc` файл **0777**,
  sysfs-группы без ABI-доки, ~3k строк мёртвого кода (ESD/gesture/test
  выключены, Longcheer-хуки, `#if LINUX_VERSION_CODE` гварды), свои
  printk-макросы вместо dev_*.
- **ufs-qcom**: `qcom,device-reset-mmio` — прямой ioremap TLMM мимо
  pinctrl; DT-кнобы `qcom,disable-devfreq|disable-runtime-pm|
  force-legacy-dme-quirks`; dev_info в probe.
- **шум**: `mdt_loader` dev_err-диагностика на каждый remoteproc-старт
  (видно в baseline dmesg), ufs dev_info.
- **bisect-костыли**: pinctrl-msm `reserved_gpios`/optional-IRQ,
  hci_qca `msleep(200)`×3, ipa_main ручная PIL-последовательность.
- **апстримабельно как есть**: ath10k freq-guard, qcom_scm allowlist,
  clk.c orphan reparent, gpucc Zonda, camss sm6225-таблицы.

### План стабилизации (цель)

**Цель:** kernel.patch без вендор-мусора и небезопасных костылей —
либо чистые upstream-style правки, либо задокументированные quirks;
каждая итерация проверяется диффом против baseline (0 регрессий).

- Фаза 1 (без риска): mdt dev_err, fts_spi proc-0777 + мёртвый код,
  ufs dev_info → dev_dbg.
- Фаза 2 (структура): ufs device-reset → `device-reset-gpios` через
  pinctrl; закрыть pinctrl-msm bisect-костыли; ipa ret-обработка.
- Фаза 3 (оптимизация): root-cause icc-rpm NoC keep-alive; вернуть
  UFS clk scaling если стабильно; ревизия FTS_*_EN и DT-нод.
- Фаза 4: оформить upstream-кандидаты отдельными патчами.

Не трогаем: звук (pulse/ASoC), SIM/LTE (нет карты), soundwire-таблицы.

---

## Сессия 2026-10-17 (вечер): Фаза 1 — PASS

**Сделано** (в `out/src/linux`, патч регенерирован):
- `mdt_loader.c`: убран `dev_err("mdt %s: relocate=...")` — спамил при
  каждом старте zap/adsp/modem; реальные ошибки остались.
- `ufs-qcom.c`: 6× `dev_info` → `dev_dbg` (forced ref_clk, clk scaling
  off, RPM off, RESET MMIO, vddp-ref-clk, reset toggled).
- `fts_spi`: **proc-канал `/proc/ftxxxx-debug` удалён целиком** (~450
  строк: fts_debug_write/read в двух API-вариантах, fops, create/
  release, struct ftxxxx_proc, PROC_* дефайны) — закрыт world-writable
  0777 proc-файл. Удалён `#if 0`-блок Android/Longcheer (~208 строк),
  мёртвая ветка `fts_tp_class`/`device_create`, lct-стабы и вызовы
  (procfs/ts_variant/reset_gpio/gesture_status), decl'ы в core.h.
  Живое: sysfs-атрибуты, gesture (lct_fts_tp_gesture_callback),
  rw_op/tprwreg. Итог: ex_fun.c 1393→718 строк, драйвер ~−750 строк.

**Проверка на железе** (fastboot boot boot-linux.img, kernel #2):
- `is-system-running: running`, **0 failed юнитов**
- dmesg-дифф против baseline: удалены 3× `mdt relocate` и 4× ufs
  dev_info — как задумано; новых warnings/errors нет. rodata −4 KiB.
- fts_ts на event4, `/proc/ftxxxx-debug` отсутствует, UFS HS_GEAR3,
  camss 33 entities, remoteproc0/1 running, Wi-Fi/BT/sound на месте.
- Единственное отличие: `hci0: command tx timeout` на втором init
  (t=32с) — самовосстановился (firmware докачан, Powered:yes);
  известная гонка hci_qca, не связана с правками. Помечено для Фазы 2.

**Срез:** /tmp/baseline-new.txt, /tmp/dmesg-new.txt (host).

**Состояние устройства:** работает Ubuntu на новом ядре (boot via
fastboot, НЕ прошит). boot_b = старое ядро #1 — безопасный fallback.

**Следующее:** Фаза 2 — ufs device-reset → `device-reset-gpios` через
pinctrl (убрать ioremap-хак), pinctrl-msm bisect-костыли, ipa ret.

---

## Сессия 2026-10-17 (ночь): Фазы 2+3 — PASS с находкой

**Фаза 2 — структурные правки:**
- `ufs-qcom.c/h`: удалён весь `qcom,device-reset-mmio` fallback —
  оказался **мёртвым кодом**: DT уже нёс `reset-gpios = <&tlmm 120>` +
  pinctrl-state, и baseline показывал «reset toggled (gpio)». ioremap
  TLMM больше не существует в драйвере. Комментарий «TLMM stub crashed»
  устарел — pinctrl-путь работает.
- `pinctrl-msm.c`: bisect-костыли закрыты — `platform_get_irq_optional`
  → обратно на обязательный `platform_get_irq` (summary IRQ + touch IRQ
  gpio80 работают), `if (soc_data->ngpios)`-гард убран, `dev_info` в
  probe → `dev_dbg` (upstream-форма). Остались только легитимные quirks:
  `reserved_gpios` (разреженная карта TLMM khaje — MMIO к EAST/SOUTH
  виснет), `msm_gpio_irq_quiesce` (маска+ack до регистрации summary IRQ).
- `ipa_main.c`: `qcom_scm_pas_shutdown` перестал затирать `ret` —
  явный `(void)`-стиль вызов с комментарием (expected-fail на холодную).

**Фаза 3 — оптимизация:**
- `icc-rpm` keep-alive: **root-cause задокументирован как quirk** —
  downstream держит NoC intf clocks включёнными; их снятие вешает
  MMIO-доступ (USB3 PHY pokes → hard hang). Отключать отдельные клоки —
  рискованно (каждый эксперимент = hard hang). Оставлено с комментарием.
- **UFS clk scaling — ПОДТВЕРЖДЕНО СЛОМАНО**: снял
  `qcom,disable-devfreq`, загрузился чисто (HS_GEAR1 в idle), но под
  dd-нагрузкой очередь UFS встала колом (dd не завершился, shell в D-
  state, лечится только sysrq-reboot). Флаг возвращён с уточнённым
  комментарием. Вывод: квирк нужен и задокументирован.
- `fts_spi`: `FTS_REPORT_PRESSURE_EN=0` (ABS_MT_PRESSURE убран —
  libinput он не нужен); `arch/arm64/configs/rain_defconfig` удалён
  из патча (мёртвый — источник правды `kernel.config`).
- Шум: `q6core` «LPASS npa/island OK» dev_err→dev_dbg (success-path
  на error-уровне), `phy-snps-femto` `rain: usb gdsc` dev_info→dev_dbg.

**Проверка (kernel #4, fastboot boot):** `running`, 0 failed,
HS_GEAR3, remoteproc0/1 up, fts_ts event4, camss 33 entities,
BT UP RUNNING, Wi-Fi online. dmesg-дифф: новое — 3 строки
`q6afe AFE failed to vote`/`Unknown cmd 0x100f4` (гонка APR при
подъёме ASoC, карта probed нормально — флэки); убрано — весь
целевой шум. `hci0 tx timeout` на этом буте не повторился — флэки.

**Отложено сознательно:** аудит всех status="okay" DT-нод по железу
(37 нод — нужен отдельный прогон), `qcom,force-legacy-dme-quirks`,
`qcom,auto-hibern8-us` — живые квирки.

## Фаза 4 — upstream-кандидаты (список для оформления)

Развёрнутое разбиение на серии с зависимостями и статусом —
`docs/UPSTREAM-SERIES.md`. Краткий список:
1. `ath10k/wmi.c` — freq=0 guard (чистый fix, безвредный).
2. `qcom_scm.c` — `xiaomi,fog` в qseecom allowlist.
3. `clk.c` — orphan reparent fix (со ссылкой на upstream-дискуссию).
4. `gpucc-sm6375` — Lucid→Zonda (правильный silicon fix).
5. `camss` sm6225 csiphy-3ph-1-0 таблицы + camss.c wiring.
6. `pinctrl-khaje` + `reserved_gpios`/`irq_quiesce` в pinctrl-msm
   (новый SoC-драйвер + quirk-механизм — отдельная серия).
7. `ufs-qcom` khaje-квирки: ref_clk force-on, vddp-ref-clk —
   как DT-quirk серия (с disable-devfreq как документированный
   fallback).
8. `ipa_main` full-region PAS_MEM_SETUP + pre-init PIL-последовательность
   — обоснование PT_NULL scratch уже в коде.
9. `dts`: sm6225.dtsi + xiaomi-fog/rain — после чистки выше.

## Финал сессии 2026-10-17: все фазы закрыты

- `boot_linux` = **ядро #5** прошито в `boot_b` через Android+Magisk dd
  (sha256 img: 252a0440…). Слот B грузит Ubuntu на чистом ядре:
  `running`, 0 failed, UFS HS_GEAR3, все подсистемы на месте.
- Слот A (Android `CP2A.260605.016`, Magisk) — нетронутый fallback.
- Коммиты сессии: `c3a495b` (Ф1), `7d1cebc` (Ф2+Ф3),
  `e97069b` (gitignore sensors/).
- Итог патча: −1900+ строк от начала сессии; proc-0777 канал удалён,
  MMIO-хаки ufs/pinctrl закрыты или задокументированы как quirks.

---

## Сессия 2026-10-17 (вечер): UFS devfreq — ROOT CAUSE найден

Кросс-проверка с веткой `main` (`fix/sm6225-ufs-memory-stability`,
патч `redmi10c-mainline-20260831`): старая доказанно-стабильная
конфигурация имела `qcom,disable-devfreq` **+ `qcom,disable-runtime-pm`**
(ufshcd: `pm_runtime_forbid` + `ahit=0`). При переносе в новое дерево
`disable-runtime-pm` и его обработка в `ufshcd.c` потерялись; вместо
этого в DT осталось `qcom,auto-hibern8-us = <5000>` — **мёртвое
свойство**, его никто не парсит (ahit брался дефолтом 150мс).

Вывод: клин был НЕ от clk scaling сам по себе, а от гонки
«выход из hibern8 (150мс) ↔ devfreq scale transition».

**Эксперимент (ядро #6):** devfreq ON + `qcom,disable-runtime-pm`
(вернул обработку ahit=0/pm_runtime_forbid в ufshcd.c) + убран
мёртвый `auto-hibern8-us`.

**Стресс-результаты:** 800 МБ dd+fsync — 1.65с @507МБ/с (раньше этот
же тест вешал систему навсегда); 89 принудительных переходов
50↔200МГц (userspace governor) под ~3ГБ смешанного read/write —
**0 UIC-таймаутов**, `running`, 0 failed. `trans_stat` подтверждает
реальные транзишны; `auto_hibern8 = 0`, `runtime_status: active`.

**Итоговая конфигурация UFS:** devfreq включён (масштабирование
core clk 50↔200МГц), auto-hibern8/runtime-pm выключены — как в
доказанно-стабильной старой сборке. `disable-devfreq` больше не нужен
и удалён из DT.

**Комплексный стресс (ядро #6, ~18 мин аптайма):** 8 CPU-воркеров
(sha256 по /dev/urandom) + UFS mixed I/O 2ГБ с devfreq-флипами +
adb push/pull 1ГБ через USB-гаджет + ping -i0.2 параллельно.
Результат: 129 devfreq-транзишнов, **0 ошибок в dmesg** (новые строки —
только «scaling suspended, skip» штатных skip'ов), `running`, 0 failed,
remoteproc0/1 running, fts_ts на месте, `/proc/ftxxxx-debug` отсутствует,
Wi-Fi 6GHz 1170Mbit, BT UP RUNNING, температуры 32-36°C, md5 через
гаджет совпадает — коррупции нет.

**Продолжение чистки (ядро #9):** Focaltech warnings устранены
(пустые FTS_*-макросы → do-while, `__maybe_unused`, whitespace);
`qcom,force-legacy-dme-quirks` снят — **3 холодных бута подряд UFS
линкуется чисто без DME-квирков** (quirk был нужен только на раннем
bring-up с битыми PHY-таблицами). Прошито в `boot_b`
(sha256 d12fc7f4…): `#9`, `running`/`0 failed`, remoteproc0/1 running,
devfreq активен (13 транзишнов за первую минуту).

## Сессия 2026-10-17 (ночь): icc-rpm keep-alive — root cause + минимум

**Блок A (NoC interface clocks):** в `sm6115.c` intf-списки держали
постоянные SMD-голоса `cpu_axi`+`ufs_axi`+`usb_axi` (snoc) и `usb_axi`
(cnoc). Бисект на устройстве — каждый тест отдельный образ через
`fastboot boot`:

| Тест | Конфиг | Итог |
|---|---|---|
| T1–T4 | по одному клоку снят, `clk_ignore_unused` в cmdline | «PASS» — **ложный**: флаг физически держал клоки, голоса ни на что не влияли |
| T5a | все голоса, БЕЗ `clk/pd_ignore_unused` | PASS — платформа живёт без blanket-флагов, драйверы клеймят свои клоки честно |
| T5b | 0 голосов, без флагов | hang ~2 мин → `hardlockup_panic` → ребут в fastboot |
| T6 | `usb_axi` snoc+cnoc | PASS 5+ мин |
| T7 | `usb_axi` только cnoc | hard hang до энумерации гаджета |
| T8 | `usb_axi` только snoc | крах → ребут |

**Вывод:** `usb_axi` обязателен на ОБОИХ NoC (snoc — путь к DWC3 MMIO,
cnoc — `qhs_ahb2phy_usb` к PHY-регистрам); поодиночке вис в обоих
случаях — ровно «USB3 PHY pokes hard-hang» из исходного комментария.
`cpu_axi`/`ufs_axi` доказанно не нужны (их пути держат собственные
клоки CPU/UFS-драйверов) и удалены.

**Побочная находка:** без `clk_ignore_unused`/`pd_ignore_unused`
платформа грузится чисто — реальное гейтинг-выключение незавоёванных
клоков технически работает (потенциальный выигрыш по idle-потреблению;
отдельный эксперимент — флаги пока оставлены из-за Ubuntu-пути с
дисплеем/модулями).

**B3:** `dsi_phy_7nm.c` — вернул PLL lock failure в `pr_err`/
`DRM_DEV_ERROR` (было опущено до debug — при реальном фейле был бы
чёрный экран без диагностики).

**C1:** аудит `status="okay"` — все 37 нод fog.dts + 6 в dtsi
маппятся на реальное пробившееся железо (i2c: камеры/VCM/EEPROM/
NFC/фонарик/топливомер/PA/зарядка/Type-C; soundwire wcd937x×2;
CCI; IPA; GPU/SMMU; UFS; DSI). Фантомов нет, в dmesg 0 probe-фейлов.
Отключать нечего.

**Инструмент:** `rebuild-rescue.sh` паковал initramfs через `xz -9e`
(заметно медленнее распаковка ядром при буте) — переведён на
`zstd -19` как в `build.sh` (`CONFIG_RD_ZSTD=y`). Образ 52.8МиБ —
в лимит `fastboot boot` проходит.

## Блок B1 (hci_qca timing/tx timeout) — итог расследования

**Фантомы:** `hci0: Frame reassembly failed (-84)` — безвредный шум
(есть и в baseline на рабочем буте — boot-spew чипа, парсер сам
ресинхронится). Единичный `0xfc00 tx timeout` на первой попытке —
тоже флэк, ретрай чинит.

**Реальный баг (редкий, ~1/10 power-cycle при off=2с):** чип уходит в
wedge — шлёт мусор в UART и не отвечает на 0xfc00 по всем 4 ретраям
init'а → адаптер дерегистрируется, `btmgmt index list` пуст. Wedge
не вечный (~60с), после чего unbind/bind `hci_uart_qca` на serial0-0
поднимает чип без ребута. Механизм похож на несостоявшийся off-pulse
@2400 (чип остаётся в run-FW на 3.2Мбод — «мусор» = его ответ на
неправильном боде) — все BT-рельсы на fog общие с Wi-Fi/др. и
физически не роняются (l9=4 юзера, l16/l17/l23 общие), поэтому
пульсы — единственный реальный канал управления питанием.

**Испробовано и отклонено:** восстановление `qca_send_pre_shutdown_cmd`
для wcn399x — на частоту wedge не влияет; `msleep(600)` после on-пульса
— тоже (откачено до 200). Preshutdown оставлен — это upstream-путь,
обоснование его пропуска невалидно при выключенном IBS, дифф к
upstream меньше.

**Оставшийся дебт:** надёжной посадки off-pulse нет; глубокий wedge
требует unbind/bind (не ребут). Полевое воздействие низкое —
`power off` в проде почти не дёргают.

## Блоки B2 / B4 / C2 — закрыты (2026-10-17)

**B2 — Q6AFE `AFE failed to vote`/`Unknown cmd 0x100f4`:** это
APR-команда `AFE_CMD_REMOTE_LPASS_CORE_HW_VOTE_REQUEST` (0x100f4) в
upstream `q6afe.c` — кодек голосует за LPASS HW-блок до готовности
AFE-сервиса на ADSP, ответ приходит поздно/не по порядку → `dev_err`.
На проверочном буте (kernel #9, boot_b) **не воспроизвелось** — карта
probed чисто, `aplay -l` видит XiaomiRedmi10C/MultiMedia1. Вывод:
периодический стартовый шум гонки APR↔ADSP, upstream-лог,
функционального эффекта нет (карта всегда поднимается). Код не тронут —
«фикс» = маскировка upstream-дiagnostics, не нужна.

**B4 — FTS ESD check:** `FTS_ESDCHECK_EN=0` оставлен **сознательно** —
реализация esdcheck уже удалена в вендорной чистке (`focaltech_stubs.c`
содержит заглушку `fts_esdcheck_init()→0`). Включение потребовало бы
возврата polling-workqueue с чтением магических регистров панели —
vendor-код без подтверждённой проблемы: тач стабилен в стресс-тестах,
ошибки SPI-шины самодиагностируются. Зафиксировано.

**C2 — upstream-серии:** список кандидатов (раздел «Фаза 4» выше)
развёрнут в `docs/UPSTREAM-SERIES.md` — 6 логических серий в порядке
отправки + явный список сознательно-не-upstreamable quirks
(hci_qca-тайминги, icc-rpm usb_axi keep-alive, FTS-драйвер,
UFS disable-runtime-pm, DWC3 DMA-клэмп, `*_ignore_unused` cmdline).
Поведение не менялось — только организация.

**Сессия закрыта:** блоки A1+A2, B1–B4, C1–C2 выполнены; блок D
(PulseAudio auto_null, SIM/LTE, GPS/камеры/voice) отложен.
