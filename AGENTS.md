# AGENTS.md — заметки для агента

## Окружение

- Рабочий диск: `/mnt/128` (110 ГБ свободно, выделен под проект). CWD — `/mnt/128/Redmi10c`.
- `sudo` без пароля. `gh` залогинен как `Damir00109` (ssh). Git user.name/user.email не настроены.
- **Коммиты — только от Damir00109, без какой-либо атрибуции инструментов**: никаких
  `Generated with …`, `Co-Authored-By:` ботов и подобных трейлеров. Коммитить с
  `git -c user.name=Damir00109 -c user.email=internet00109@gmail.com commit`.
- Все зависимости сборки на месте: `aarch64-linux-gnu-gcc`, `adb`, `fastboot`, `mkbootimg`, `cpio`, `zstd`, `make`, `python3`.
- `out/` отсутствует — чистый клон, полная сборка с нуля через `./build.sh` (клонирует linux `v7.1.5`, busybox, tinyalsa, mkbootimg).

## Структура репозитория

Репозиторий: https://github.com/Damir00109/Redmi10c — bring-up mainline-ядра на Xiaomi Redmi 10C (`fog`/`rain`, Qualcomm SM6225 «Khaje»).

- **`kernel` (текущая ветка)** — НОВОЕ чистое дерево (orphan-история, 31 коммит, 68 файлов): `kernel.patch` поверх Linux 7.1.5, `kernel.config`, минимальный initramfs, `build.sh`, доки в `docs/`. Общей истории с `main` НЕТ (merge-base отсутствует).
- **`main`** — СТАРОЕ дерево (295 файлов): полный Ubuntu rootfs-overlay (`ubuntu/`, OpenRC, Plasma Mobile), `notes/` с логами bring-up, `patches/`, `configs/`, скрипты бэкапа. Там живёт история всей работы (Wi-Fi, BT, UFS, модем, сенсоры, десктоп).
- `feature/wifi-stable-bringup`, `fix/sm6225-ufs-memory-stability` — старые ветки от `main`.

Вывод: актуальная разработка — в `kernel`; `main` — архив/источник контекста (rootfs-overlay, notes, старые патчи).

## Сборка и загрузка

- `./build.sh` — полный цикл: clone linux v7.1.5 → kernel.patch → Image.gz + dtbs → busybox initramfs (zstd) → `out/boot-display-console-rescue.img`.
- `tools/fetch-firmware.sh` — скачивает проприетарные блобы из релиза `firmware-2026.09.25` (firmware/ + vendor-qcom/ для hexagonrpcd).
- `tools/rebuild-rescue.sh` — быстрая пересборка в уже настроенном дереве (initramfs через `zstd -19`, как build.sh — 2026-10-17).
- `tools/boot-and-check.sh` — загрузка + проверка.
- `regen-patch.sh` — перегенерация `kernel.patch` из дерева `out/src/linux`.
- Загрузка на устройство: `fastboot set_active b && fastboot reboot bootloader`, затем `fastboot boot out/boot-display-console-rescue.img` (слот B, ничего не прошивается; Android цел на слоте A; rootfs — Ubuntu на разделе `cust`). Лимит `fastboot boot` ~57.5 МиБ.
- Консоль на устройстве: USB gadget ACM → `screen /dev/ttyACM0 115200` (иногда нужен Ctrl-C для prompt'а), плюс shell на экране.

## Статус железа (STATUS.md, обновление 2026-10-06)

Работает: экран DSI, USB gadget, UFS+SD, Wi-Fi (ath10k/WCN3990), BT+A2DP, тачскрин FT8006S (SPI), LTE через IPA v4.2/QMAP (~9 Мбит/с), модем+QMI/ModemManager, звук (динамик + гарнитура FSA4480), зарядка SMB1351 + fuel gauge, thermal, cpufreq, GPU Adreno 610 (SMMU/GMU/ZAP, devfreq), сенсоры через SSC/hexagonrpcd, Plasma Mobile + Firefox, вибратор, фонарик, RTC.

Работает также задняя камера S5KJN1 (полный CAMSS-пайплайн, RAW10 4080x3072). В разработке: GPS, iio-sensor-proxy, ACDB, голос/SMS, AE/AWB для камеры (ISP-уровень), передняя камера Hi556. Не работает: сканер отпечатков.

## Соседняя папка: /mnt/128/rootfs-ubuntu

Независимая сборка rootfs Ubuntu Server 26.04.1 (resolute) для раздела `cust`.
- `build-rootfs.sh` — проверен, работает (binfmt qemu-aarch64 с флагом F, qemu копировать не нужно).
- `cache/` — cloudimg rootfs tarball скачан. `tools/rain-overlay` + `pivot-init` извлечены из ветки `main`.
- `out/linux_rootfs.sparse.img` (~1.6G) — flash: `fastboot flash cust ... && fastboot continue`.
- Модули ядра: `make ... O=<abs path> modules` + `modules_install INSTALL_MOD_PATH=out/build/linux-7.1.5` → lib/modules/`7.1.5-dirty` (O= требует АБСОЛЮТНЫЙ путь). Wi-Fi/ath10k built-in (=y); `qcom_q6v5_adsp` =m — без модулей нет ADSP/звука/сенсоров.
- Rootfs на systemd (как старая рабочая сборка). На устройстве сейчас OpenRC — overlay для него в git нет, восстанавливать с устройства или из релиза `v2026.09.06-final`.

## Ubuntu-образ: вылизанное состояние (2026-10-10)

`boot_b` = `rootfs-ubuntu/out/boot-linux.img` (pivot-init → cust), rootfs = 3G ext4 на sda8.
Загрузка ~44с (graphical.target ~22с), `is-system-running: running`, 0 failed.

- **ADB**: adbd + FunctionFS (`CONFIG_USB_CONFIGFS_F_FS=y`), composite-гаджет ADB+ACM `18d1:4ee7`, ключ хоста в `/etc/adb/adb_keys`. Без F_FS гаджет не поднимается вообще.
- **Wi-Fi**: цепочка qcom-firmware-stage→qrtr-ns→rmtfs→pd-mapper→tqftpserv→qcom-modem-start→ath10k-snoc-load→qcom-wifi-connect. Сеть `Xiaomi_E4C4` в `qcom-wifi-connect.service.d/creds.conf` (SSID в netplan-файле старый — там сети нет). Сервис убран с критической цепочки (`After=multi-user.target` drop-in) — иначе держал target +50с.
- **`/dev/null` EACCES при старте**: ядро создаёт ранние devtmpfs-ноды с mode 660 → dbus умирал до фикса udev'ом. Решено chmod'ом в pivot-init ПОСЛЕ `mount --move` (на `/newroot/dev`, иначе бьёт по затенённым статическим нодам) + `zz-dev-nodes.conf` в `/etc/tmpfiles.d`.
- **`/` принадлежал uid 1000**: `rsync -a` в смонтированный образ проставляет владельца исходной папки на корень образа → tmpfiles "unsafe path transition". Фикс в build-rootfs.sh (chown 0:0 + find -uid утечки после rsync).
- **logind**: гонка с dbus (стартовал до выдачи шины, висел без имени → мёртвая кнопка питания, user@1000 failed). Drop-in `Requires/After=dbus.service` + `Restart=always`.
- **qbootctl**: скрипт парсил `Active slot:`, бинарь пишет `Current slot:` + на холодном старте слот не читается ~7с → ретрай-цикл 30с в `qbootctl-mark.sh`.
- **networkd-dispatcher**: гонка с dbus → `Restart=on-failure` drop-in.
- **Маскирован балласт**: snapd, cloud-init*, apport, lvm2, iscsi, lxd-installer, ua-*, secureboot-db, vgauth, motd-news, update-notifier-*, xfs_scrub, mdcheck/mdmonitor, fwupd-refresh, sysstat, NM-wait-online, wpa_supplicant.service, systemd-networkd(.socket). Список — в mask-цикле build-rootfs.sh.
- **Ядро**: `CONFIG_BT_RFCOMM=y`( +TTY), `CONFIG_BT_BNEP=y` — иначе bluetoothd не поднимает RFCOMM-профили. `USB_CONFIGFS_F_FS=y` — ADB.
- **RTC**: rtc-pm8xxx читает, запись в регистры — EPERM (PMIC write-protect). **На стоке то же самое** — Android тоже читает 1970 и не пишет: `time_daemon` хранит время в `/data/vendor/time/ats_*` (8-байт ms-эпоха). Стоковый DT: без sdam/nvram/time_genoff. UEFI-путь: `xiaomi,fog` добавлен в `qcom_scm_qseecom_allowlist` → qseecom/uefisecapp поднимаются, efivars читается (15 переменных, `mount -t efivarfs efivarfs <mnt>`). Запись НОВЫХ переменных трастлет отбрасывает → `qcom,uefi-rtc-info` НЕ включать (probe падает -ENOENT). Компенсация: `fake-hwclock` (~6с в буте). Стоковый DTB: `/mnt/128/fdt-stock-fog.dtb`+`.dts`.
- **Переключение слотов без fastboot**: из Ubuntu `qbootctl -s a|b`; из Android `setprop sys.powerctl reboot,bootloader` → fastboot.
- **Ubuntu → fastboot (проверено 2026-10-17)**: `reboot bootloader` РАБОТАТЕТ из коробки — `qcom-pon` (CONFIG_POWER_RESET_QCOM_PON=y) регистрирует reboot-mode, `pm6125.dtsi` `pon@800` имеет `mode-bootloader=<0x2>`/`mode-recovery=<0x1>`. ~6с до fastboot. Никакой IMEM/syscon магии не нужно. Sysrq/EDL не трогать.
- **fastboot flash sparse — сломан на хосте**: fastboot 34.0.5-debian падает `Error reading sparse file` на чанке 2/3 детерминированно (файл валиден — simg_dump парсит). Обход — шить через Android+Magisk: `adb push img /sdcard/ && adb shell 'su -c "dd if=/sdcard/img of=/dev/block/by-name/XXX bs=4M"'` (cust=sda8, boot_b=sde33, ~103 МБ/с). Проверка: ext4 magic `53 ef` @1080, `ANDROID!` @0.
- **rootfs IMG_SIZE_MB=1800 < cust**: после прошивки образа rootfs забит 100% (cust реально ~19.1G на sda8). Лечение на живой системе: `resize2fs /dev/sda8` → 19G. Поднять дефолт в build-rootfs.sh или first-boot growpart.
- **Android slot A**: кастом-ROM `CP2A.260605.016` (Android 17), Magisk root (`su -c id` → uid 0), product=rain, stock DT "RAIN KHAJE IDP nopmi". IMEM там: `qcom,msm-imem@c125000`, `restart_reason@65c`, `restart@440b000`=qcom,pshold.
- **Baseline-срез**: `docs/baseline-2026-10-17.txt` + `-dmesg.txt` (0 warn/err), скрипт сбора `/tmp/collect-baseline.sh` — прогонять после правок и диффать. Эталон: SIM отсутствует (sim-missing — НОРМА), pulseaudio только auto_null (звук не трогаем).
- **Сеть = NetworkManager**: NM управляет ВСЕМ (wlan0 + qrtr0-модем). Профили — netplan: `/etc/netplan/90-NM-wifi.yaml` (Xiaomi_E4C4+psk), `90-NM-tele2.yaml` (APN internet.tele2.ru). `rmnet_ipa0` unmanaged (сырой транспорт). NM+ath10k зависаний НЕТ — ранние hang'и были артефактом UFS-коррупции. QMI: `qmicli -d qrtr://0`. SIM: `uim-provision.service` каждый бут ждёт USIM-приложение, приводит карту в **logical slot 1** (data-подписка) и биндит primary GW (`--uim-change-provisioning-session ...slot=N,aid=<AID>`). Quirk: MM 1.25.95 SEGV если `qmapmux0.0` уже есть при коннекте — удалять линк. ModemManager/NM НЕ маскировать.
- **DSD data-подписка (главная грабля, 2026-10-11)**: если переставлять SIM между слотами, модем может оставить data-подписку на пустой подписке → регистрация/голос идут, а bearer падает `cm error: no-service` (симптом: `state: registered`, PS attached, capability=1, но коннект не идёт ни через NM, ни через MM, ни QMI). **libqmi не умеет её ни читать, ни задавать** — из mainline не лечится. **Лечение: один раз загрузиться в Android с этой SIM** — RIL выставляет подписку, после этого она персистентна (проверено: ребут в Ubuntu → `state: connected` сам). Слоты: `mmcli -m 0 --set-primary-sim-slot=N` (надёжно), `qmicli --uim-switch-slot` — NoEffect/Internal и маппинг не персистится.
- **Не трогать `--dms-set-operating-mode=offline`** на этом модеме: застревает в offline (`InvalidTransition` на online), не отпускает ни рестартом remoteproc, ни `--dms-reset` — только холодный ребут. `--wds-create-profile` пишет профили в EFS модема (чистить `--wds-delete-profile=3gpp,N`).
- **qseecom allowlist**: правка в `out/src/linux/drivers/firmware/qcom/qcom_scm.c` (не в патче пока — `regen-patch.sh` при следующей регенерации).
- **ath10k board data**: `qcom-firmware-stage.sh` раньше линковал сырой `bdwlan.bin` как `board-2.bin` → «invalid board magic» (api2 ждёт контейнер, не голый BDF). Правильный контейнер собран из стоковых BDF (43 записи, `variant=Xiaomi_fog` = `bd3qvdfu.bin`, сток: `ro.wlan.bdf=bd3qvdfu.bin`, чип TSMC) — зашит в релизный `redmi10c-firmware.tar.zst`. Скрипт теперь: `board.bin`(api1)→`bd3qvdfu.bin` с раздела, `board-2.bin` — файл из `/usr/lib/firmware` (не шадоуить).
- **mcfg.tmp (tqftpserv)**: НЕ подавать сюда сырой MBN! Проверено: `mcfg.tmp` = tele2 `mcfg_sw.mbn` → модем грузит его как конфиг, регистрация ломается (`not-registered-searching`, `nas-network-scan` → Internal). Пустой/отсутствующий `mcfg.tmp` — правильно: модем берёт конфиг из своего PDC-стора. tqftpserv раздаёт из **`/lib/firmware`** (не из `/var/lib/tqftpserv`!). `lctoem.tmp` в стоке нет — шум безвреден.
- **Автовыбор оператора — уже работает**: в PDC модема 25 software-конфигов (все из `/vendor/firmware_mnt/image/modem_pr/mcfg/configs/mcfg_sw`, туда же `mcfg_hw/generic/common/divar/la/{7+7_mode/sr_dsds,ss}`). Модем **сам** активирует нужный по SIM (`qmicli --pdc-list-configs=software` → `tele2_russia` Active). Ничего инжектить не надо.
- **FTS-прошивка**: `focaltech_ts_fw_xinli.bin` в rootfs идентична стоковой (131072 B, md5 ae868b7…) — «boot-id CRC fail» это драйверная особенность панели, не missing firmware.
- **Слот 2 SIM — НЕ поддерживается (2026-10-11)**: регистрация/голос/SMS идут, но любой data-bearer модем отклоняет `cm error: no-service`. Перебрано всё: сессия primary и secondary, маппинг physical2→logical1 (primary) и physical2→logical2 (дефолт), свежий модем, здоровый MM, IPv4/IPv4v6, `mcfg.tmp` пусто/tele2/hw/нет, PDC Active/Inactive, профили пересозданы. В Android слот 2 работает: RIL биндит SIM как **secondary-gw-provisioning** и держит свою data-подписку (`activeDataSubId` = слот SIM) в NV модема. В libqmi/MM этого нет: DSD умеет только get-apn-info/set-apn-type/get-system-status, per-subscription QMI-клиента нет, `qmicli 1.38 --pdc-load-config` падает в segfault. Итог: `uim-provision.sh` **гасит физический слот 2** (`--uim-sim-power-off=2`) и не инициализирует его; SIM — только в слоте 1.
- **Hot-swap SIM (слот 1) — проверено физически**: `uim-hotplug.service` + `/usr/local/sbin/uim-hotplug.sh` слушают `qmicli --uim-monitor-slot-status` и на каждое изменение статуса слота перезапускают `uim-provision.sh` + `systemctl --no-block try-restart ModemManager`. Вынул/вставил SIM на ходу (слот 1) → в логе `slot status changed` → `USIM bound (slot 1)` → через ~минуту MM `connected`, NM `tele2 activated`, LTE-пинг OK. Без ребута.
- **Data-подписка модема живёт в NV и ставится только Android'ом**: после переноса SIM между слотами data в Ubuntu падает `no-service`, пока не загрузишься один раз в Android с SIM в нужном слоте (RIL выставит подписку) — потом в Ubuntu работает. Проверено дважды. Слоты: надёжно `mmcli -m 0 --set-primary-sim-slot=N` (крос-маппинг physical→logical1); `qmicli --uim-switch-slot` часто NoEffect/Internal.
- **Захват QMI на Android** (для таких расследований): `setprop persist.vendor.radio.adb_log_on 1` → **ребут** (проп читается при старте радио) → `adb logcat -b radio RILQ:V RILJ:V '*:S'` (qcril-трейс). Проверено: при hot-swap SIM Android шлёт только UIM-активацию подписки (`qcril_qmi_prov_activate_deactivate_app: Activate sub: slot N`, для слота 2 — `session_type 2`), никаких DMS/slot-mapping команд нет. Готовый лог: `/mnt/128/qmi_capture_simslot.log`.
- **BT-аудио (A2DP) — работает, 2026-10-11**: причина, по которой sink не появлялся — `--disallow-module-loading` в `pulseaudio.service`: `module-bluez5-discover` грузит `module-bluez5-device` **динамически** при подключении ушей, а флаг это блокирует (`Failed to load module for device /org/bluez/hci0/dev_...`). Плюс конфликт двух инстансов PA (системный из overlay и юзерский `user@1000`, который поднимал `rain-bt-start.sh`) — дрались за BlueZ-профили. Исправлено: флаг убран, `pulseaudio.service` enabled, `rain-bt-start.sh` поднимает системный PA. Проверено: `bluez_sink.*.a2dp_sink` RUNNING, кодек SBC, музыка играет (`paplay --device=bluez_sink.<mac>.a2dp_sink file.mp3`, `PULSE_SERVER=unix:/var/run/pulse/native`). Локальная карта (динамик/провод) при этом всё ещё не работает — BT-аудио идёт в обход.
- **Suspend/resume (2026-10-11)**: поддерживается только `s2idle` (`/sys/power/mem_sleep` = `[s2idle]`, deep нет). **С включённым Bluetooth suspend НЕ работает**: `hci_uart_qca serial0-0: PM: dpm_run_callback(): qca_suspend returns -110` → `PM: Some devices failed to suspend` → ядро отменяет сон. С **выключенным BT сон уходит и просыпается штатно** (проверено, пробуждение кнопкой Power). Нюанс: USB-гаджет (adb) после resume не возвращается — `adb` остаётся `offline` до перезагрузки (локальный баг гаджета, не системы).
- **ADSP/sensors/audio — корневой баг найден (2026-10-11)**: `rain-stable-boot.sh` глушил **все** remoteproc'и в цикле (`echo stop > /sys/class/remoteproc/remoteproc*/state`), включая **ADSP** (remoteproc1). Модем потом поднимал `qcom-modem-start`, а ADSP — никто, и он оставался `offline`. Из-за этого: (1) не работали сенсоры (нет `/dev/fastrpc-adsp`), (2) не регистрировалась ASoC-карта (`no soundcards`, `q6afe`/`AFE failed to vote`), (3) LPASS-клоки снимались и кодек WCD937x флудил SoundWire, (4) на PDR-индикации ADSP падало ядро в `clk_q6dsp_prepare` (NULL-deref). Исправлено: скрипт глушит **только модем** (`[ "$(cat $RP/name)" = modem ] || continue`). Плюс защита в ядре (`sound/soc/qcom/qdsp6/q6dsp-lpass-clocks.c`: `q6dsp_cc_get()` перед prepare/unprepare/vote — в `kernel.patch`). После этого ADSP `running`, `/dev/fastrpc-adsp` появляется, сенсоры и **аудио работают**.
- **Сенсоры (SSC/hexagonrpcd)**: `sensors-setup.service` (реестр с `persist` → `/usr/share/qcom/sm6225/Xiaomi/rain`, socinfo, `modprobe fastrpc`), `hexagonrpcd-adsp-rootpd` + `hexagonrpcd-adsp-sensorspd` (`-R /usr/share/qcom/sm6225/Xiaomi/rain`, `LD_LIBRARY_PATH=/usr/lib/musl`, юзер `fastrpc`), udev `10-fastrpc.rules` (owner fastrpc) + `90-accel-mount.rules`. Бинари/либы — musl-сборки из pmOS, лежат в релизном тарболе (`sensors/`, fetch-firmware.sh). **Важно**: musl-загрузчик `/usr/lib/ld-musl-aarch64.so.1` ставить с правами **755** (с 644 — `status=203/EXEC`). Чтение: `LD_LIBRARY_PATH=/usr/lib/musl ssccli --sensor accelerometer|light|proximity`. Аксель: X=9.8, свет ~69 Lux, proximity FAR. Фирмварь отдаёт all-zero mount matrix → оси повёрнуты (лечится `ACCEL_MOUNT_MATRIX` для iio-sensor-proxy).
- **GPS/GNSS — работает (2026-10-11)**: GNSS-движок живёт **в модеме** и доступен через **QMI LOC/PDS** (QRTR service 16, `qmicli --loc-*`). Ядерного драйвера нет и не нужно. Путь: модем → **ModemManager Location** (capabilities `3gpp-lac-ci, gps-raw, gps-nmea, agps-msa, agps-msb`) → **`gnss-share`** (`device_driver="mm"`, `/etc/gnss-share.conf`) → NMEA на `/var/run/gnss-share.sock` для geoclue/приложений. `rain-gps.service` (`rain-gps-start.sh`) на загрузке включает `gps-nmea`+`gps-raw` у MM (Location надо включать явно; `gnss-share` сам ставит только refresh rate). Грабли: `gnss-share` требует группу **`geoclue`** (иначе `status=1`); `gnss-share` конфликтует с `gpsd` (ставить только его); qmicli даёт **одну** LOC-операцию за вызов (start+follow вместе нельзя). Работает **без SIM** (standalone). Фикс требует обзора неба — в помещении приходят только `$GPGSV` (~10 спутников, 34–39 dBHz), позиции нет. Проверка: `mmcli -m 0 --location-get`, чтение сокета (python/`socat`).
- **GNSS: почему A-GPS не работает в mainline (2026-10-11)**. Стоковый vendor достаётся из `super.img` (LP-метаданные: geometry@0x1000, header@0x3000, таблицы **после** заголовка; `vendor_a` offset=**7119831040** size=920.7 MiB — ошибка в 1 МиБ даёт мусор вместо ext4). Стоковый `/vendor/etc/gps.conf`: `CAPABILITIES=0x17` (MSA|MSB|SCHEDULING|ON_DEMAND_TIME), `LPP_PROFILE=3`, `A_GLONASS_POS_PROTOCOL_SELECT=0xf`, `SUPL_VER=0x20000`, `XTRA_*` + `NTP_SERVER=time.xtracloud.net`, **`AGPS_CONFIG_INJECT=1`** (HAL инжектит бандл A-GPS-конфига в модем через QMI), `RF_LOSS_*` для GPS L1/L5, GLO, BDS/B2A, GAL/E5, NavIC → движок поддерживает эти созвездия/диапазоны. `izat.conf`: `xtra-daemon` ENABLED, `lowi-server`, `xtwifi-*`, `SUPL_WIFI=BASIC`, NLP=Google. **Созвездий в конфигах нет** — их ставит дефолт прошивки или HAL через QMI. Поэтому MM (`--location-set-supl-server`, `--location-enable-agps-msa`) модем отвергает («Failed to receive operation mode indication», режим остаётся `standalone`), а инъекция орбит — «unsupported». Копии конфигов: `/mnt/128/stock-fw/gnss/`. Чтобы включить A-GPS/созвездия — нужен кастомный QMI LOC-клиент, повторяющий HAL (Set Operation Mode + Set Server + Set Protocol Config + Set GNSS Constellation + Inject Predicted Orbits).
- **A-GPS из mainline — `rain-loc-tool` (2026-10-11)**: MM не умеет то, что делает вендорский HAL: он ждёт **индикации**, которых эта прошивка не шлёт (`--location-set-supl-server` → таймаут, `--location-enable-agps-msa` → «Failed to receive operation mode indication», inject → «unsupported»). Но обычные QMI-**ответы** проходят: `rootfs/tools/rain-loc-tool.c` (собирается в `build-rootfs.sh` → `/usr/local/bin/rain-loc-tool`) вызывает libqmi напрямую и применяет бандл: `time` (инъекция UTC — **критично**: без неё движок остаётся в measurement-режиме: `$GPGSV` с пустыми PRN и без `$GPGGA`; после неё появляются `$GPGGA`/`$GPGSA`), `orbits` (XTRA — `--loc-get-predicted-orbits-data-validity` показывает 168 ч), `server` (SUPL), `mode msb|msa`. `rain-gps.service` применяет всё на загрузке: модем → конфиг → перезапуск MM (LOC-вызовы глушат NMEA-сессию MM) → включение `gps-nmea/raw/3gpp`. Сборка требует `gcc libqmi-glib-dev pkg-config` (пуржатся после компиляции). Фикс всё равно требует обзора неба: за стеклом ~10 SV при 30–39 dBHz, `$GPGGA` с fix quality 0.
- **GPS: фикса нет даже с полным A-GPS (2026-10-11)**. После применения всего бандла (`rain-loc-tool`: время + SUPL + MSB + XTRA) движок переходит в режим решения — идут `$GPGGA`/`$GPGSA` (192 GGA за 200 с), но **`$GPGSA,A,1,,,,…` пустой**: ни одного отслеживаемого SV, а `$GPGSV` приходит **без PRN/elev/azim — только SNR** (30–41 dBHz). То есть движок видит энергию в диапазоне, но не трекает спутники → похоже на приёмный тракт (антенна/LNA), а не на конфиг. Диагностика: сверить с Android на том же месте (если Android фиксится — в mainline не хватает чего-то из HAL-цепочки, если нет — условия приёма). Остальное в GPS работает: 3GPP-сота, MM Location, `gnss-share`, A-GPS-бандл применяется.
- **`gnss-keepalive` — обязателен (2026-10-11)**: `gnss-share` **глушит приёмник, когда отключается последний клиент** — поэтому `mmcli -m 0 --location-get` после первого чтения отдаёт пустоту (NMEA перестаёт поступать), и фикс, которому нужны минуты непрерывного трекинга, не может состояться. `gnss-keepalive.service` (`/usr/local/sbin/gnss-keepalive`) держит сокет открытым постоянно и пишет NMEA в `/var/log/gnss-nmea.log`. Проверено: с ним MM отдаёт NMEA непрерывно. Фикса всё равно нет: после 5 мин непрерывного трекинга `$GPGSA` пустой (ни одного SV), `$GPGSV` без PRN (только SNR 37–41) → движок в acquisition и не захватывает спутники. Остаётся либо условия приёма (стекло), либо созвездия: `Set GNSS Constellation` в libqmi **не реализован** (ни в .so, ни в заголовках), нужен сырой QMI.
- Известные мелочи: `hci0 Frame reassembly failed` — безвредный шум; `fastboot`+`upower`+`fake-hwclock` поставлены в rootfs.

## Грабли (важно)

- `QCOM_CLK_SMD_RPM` и `SM_GPUCC_6115` обязательны в конфиге — без них всё висит на EPROBE_DEFER.
- Ноде вибратора (gpio36) нельзя давать pinctrl — gpiod/pinctrl дерутся за пин.
- devmem-поки по TLMM на этом чипе вешают шину — использовать `vibtest` (EV_FF ioctl).
- Магнитометра/компаса в железе нет. devinfo slot byte не работает — слоты переключаются через GPT type GUID + UFS boot LUN.
- **i2c-geni SE-DMA на Khaje вешает шину на transfer ≥32 байт.** Любой transfer, где `i2c_get_dma_safe_msg_buf(msg, 32)` возвращает буфер (т.е. `len >= 32`), уходит в SE-DMA и никогда не завершается — задача в D-state, shell мёртв (hung-task). FIFO-путь работает. Лечится property `qcom,no-dma` (`drivers/i2c/busses/i2c-qcom-geni.c`, выставлено на `&i2c1` в `sm6225-xiaomi-fog.dts`). Подробности — `docs/FUEL-GAUGE-SH366101.md`.
- **Fuel gauge SH366101 читает gaugeinfo-блоки (32 B) только через ALTMAC 0x3E → ALTBLOCK 0x40 → ALTCHK 0x60, unseal не нужен**; на i2c1 обязателен `qcom,no-dma` (см. выше). 5 с — SBS, 30 с — блоки; диагностика `cat /d/sh366101/dump`.
- **USB полностью во владении ядра** (проверено холодным бутом 2026-10-18): QSCRATCH `0x4ef8800` — обёртка `usb@4ef8800` (`qcom,sm6115-dwc3`/`qcom,dwc3`) с клоками/ресетом; UTMI-клок выбирается свойством **`qcom,select-utmi-as-pipe-clk`** — **без него первый bind гаджета падает с `dwc3: failed to enable ep0out`** (QSCRATCH_GENERAL_CFG не получает `PIPE_UTMI_CLK_SEL`), и USB не энумерится вообще. HS-PHY оверрайды и `GUSB2PHYCFG0` выдаёт драйвер теми же значениями, что раньше писал userspace — старые `devmem`-пьоки из `pivot-init`/rescue-init удалены как избыточные (в `pivot-init` MMIO больше нет вообще).
- **`fastboot boot` маскирует cold-boot проблемы USB**: ABL оставляет клоки/контроллер включёнными для fastboot, поэтому образ, загруженный через `fastboot boot`, может работать, а тот же образ после `fastboot flash` + холодный ребут — нет. Проверять USB только холодным бутом.
- **Логи Ubuntu-стороны можно читать из rescue**: rescue-init монтирует `cust` и печатает `/var/log/usb-adb-gadget.log`; `pivot-init` умеет писать диагностику в `/dev/kmsg` (попадает в `/var/log/dmesg`). Из Android то же читается через `su -c 'mount -t ext4 -o ro /dev/block/by-name/cust /mnt/cust'`.
- **Консоль rescue = USB ACM**: если `stty`/termios ругается «Inappropriate ioctl» — в `/dev/ttyACM*` лежит обычный файл (создан failed-redirect'ом), удалить; номер порта может съехать на ttyACM1. Надёжнее python-termios, чем `stty -F`.

## UFS на khaje (2026-10-17, проверено стрессом)

- **Стабильная конфигурация: `qcom,disable-runtime-pm` (ahit=0 + pm_runtime_forbid в ufshcd.c), devfreq ВКЛЮЧЁН.** Auto-hibern8 + devfreq вместе = клин очереди (гонка hibern8-exit ↔ scale transition), dd виснет насмерть. По отдельности каждый работает; hibern8 выключен — линк всегда в активном gear, скейлится только core clk (50↔200 МГц).
- `qcom,auto-hibern8-us` — мёртвое свойство, драйвером не парсится.
- Reserved-memory carveouts в sm6225.dtsi (pil_modem/adsp/ipa/gpu/…) обязательны — без них page cache коллидирует с памятью remoteproc'ов (исторический UFS-corruption баг, ветка fix/sm6225-ufs-memory-stability на main).

## Камеры (SM6225 CAMSS/TFE) — bring-up 2026-10-11

Железо (из Android-пропов + стокового dtbo `fdt-stock-fog.dts`):

| Камера | Сенсор | Модуль | CSIPHY | CCI | MCLK / RESET |
|---|---|---|---|---|---|
| Main | Samsung **S5KJN1** (50 МП) | sunny | 0 | m0 | GPIO27 / GPIO18 |
| Front | Hynix **HI-556** (5 МП) | aac | 2 | m1 | GPIO20 / GPIO24 |
| Depth | OmniVision **OV02B1B** (2 МП) | aac | 1 | m1 | GPIO21 / GPIO65 |

- Блоки: TFE 340 (`tfe0/1/2` @ `0x5c6e000/0x5c75000/0x5c7c000`), CSID 340, CSIPHY v1.2.1 ×3
  (@ `0x5c52000/0x5c54000/0x5c56000`), CCI v1.2 @ `0x5c1b000`, CPAS @ `0x5c11000`.
  SM6225 = QCM2290 + третий CSIPHY/CSID/TFE (адреса/IRQ первых двух совпадают).
- **Что уже сделано (работает на устройстве)**:
  - `camss.c`: добавлены `sm6225_resources` (3× CSIPHY/CSID/TFE), переиспользуется
    обработка `CAMSS_2290` (csiphy_ops_3ph_1_0 / csid_ops_340 / vfe_ops_340);
    compatible `qcom,sm6225-camss` в `camss_dt_match`.
  - `sm6225.dtsi`: узлы `camss@5c11000` (top/csid0-2/csiphy0-2/tpg/vfe0-2, GCC CAMSS-клоки,
    IRQ, ICC, SMMU, `GCC_CAMSS_TOP_GDSC`) и `cci@5c1b000` (`qcom,qcm2290-cci`,
    `qcom,msm8996-cci`); camera-pinctrl (`cci0/1`, `mclk0-3`, `cam_reset0-2`).
  - `fog.dts`: `&camss`/`&cci0` okay; CSI PHY-рельсы `pm6125_l4a` (0.88 V) / `pm6125_l18a` (1.232 V).
  - `pinctrl-khaje.c`: из `khaje_reserved_gpios` убраны пины камеры (18,20-24,27-30,65) —
    без этого `pinctrl` не мог их замуксить (`msm_gpio_init_valid_mask` помечал их невалидными).
  - config: `MEDIA_SUPPORT`, `MEDIA_CAMERA_SUPPORT`, `VIDEO_QCOM_CAMSS`, `I2C_QCOM_CCI`,
    `V4L2_CCI_I2C`, `VIDEO_S5KJN1` (=y).
  - **Проверено**: `camss` probe OK (`/dev/media0`, `vfe0_video0`, `v4l-subdev0..17`),
    `cci0` probe OK и регистрирует 2 i2c-шины (`/sys/bus/i2c/devices/i2c-0`,`i2c-1` →
    `5c1b000.cci`). Ошибок pinctrl/hang нет.
- **Сенсор S5KJN1 — попытка поднять (2026-10-11), пока НЕ работает**:
  - Узел `camera@36` на `&cci0_i2c0` + `&camss port@0` уже в `fog.dts`. Драйвер
    `s5kjn1` **находится** на CCI (`i2c-0`, адрес `0-0036`), т.е. адрес 0x36 правдоподобен.
  - **reset-пин нельзя давать в pinctrl**: `gpiod` (reset-gpios) и `pinctrl` дерутся за пин
    (`cannot get reset GPIO -EINVAL`). В `pinctrl-0` сенсора оставлен только `mclk2_default`.
  - **MCLK-клок не заводится**: `gcc_camss_mclk2_clk` при enable валит
    `WARNING clk-alpha-pll.c:421 wait_for_pll` + `clk.c:1188/1047` (PLL не лочится) — надо
    разобраться с родителем/PLL для CAMSS MCLK (возможно, нужен CAMSS GDSC/CC или другой parent).
  - **PM8008 (камерный PMIC) не отвечает**: узел `pmic@8` (и пробовал `@44`) на `&i2c1`;
    `Failed to set masks in 0x916: -6` (ENXIO) → `failed to add IRQ chip` → MFD не поднимается,
    рельсы `pm8008_l*` не регистрируются. Похоже, PM8008 **не запитан**: его VIN идут с PM6125
    (не заведены) + нужен `chip-en`. В стоке: `qcom,pm8008@8 { reg = <0x88> }`,
    `qcom,pm8008-chip@900`, `pm8008-chip-en`; IRQ = TLMM gpio25 (уже убран из reserved).
    Рекомендация: найти реальные VIN-рельсы PM6125 из стока/схемы, завести `chip-en`,
    и только потом ждать probe. `CONFIG_MFD_QCOM_PM8008`/`REGULATOR_QCOM_PM8008` уже `=y`.
  - Userspace: `v4l-utils`/`libcamera` в rootfs нет — нужен `media-ctl`/`v4l2-ctl`
    для настройки пайплайна и захвата (сейчас можно проверить только наличие `/dev/media0`).
  - Depth (OV02B1B) — драйвера в mainline нет вообще; front (HI-556) — драйвер есть, но ACPI-only
    (нет `of_match_table`), нужна доработка под DT.
  - Сборка `s5kjn1`/`pm8008` как модулей — не делать; они `=y` (иначе не грузятся без sync модулей).
- Полезное: vendor внутри `super` (sda9) монтируется через LP-метаданные:
  geometry @0x1000, header @0x3000 (парсер `/tmp/lpparse.py`); vendor_a extents →
  `mount -o ro,loop,offset=<sectors*512> /dev/sda9 /mnt/v`.
  Стоковый ROM: `/mnt/931/fog_global_images_V14.0.9.0.TGEMIXM_13.0/`.
  Сборка/тест: `REDMI=/mnt/128/Redmi10c bash rootfs/tools/pack-boot-linux.sh` →
  `fastboot boot rootfs/out/boot-linux.img` (неразрушающе).
- **USB на 4/64 (`snps,dis-64bit-dma` — НЕ удалять)**: DWC3 на Khaje декларирует
  `GHWPARAMS0.AWIDTH=64` и `dwc3_probe` ставит 64-битную DMA-маску, но реально к DRAM
  проведены только 32 бита и контроллер НЕ за SMMU (в стоке он за `apps_smmu`
  SID 0x120 с пулом <4 ГБ). На платах с RAM >4 ГБ (rain 4/64) coherent-буферы
  (event/TRB) попадают в ZONE_NORMAL выше 4 ГБ → контроллер пишет «в никуда»:
  `unknown endpoint event 0`, UDC не аттачится, на хосте `-71`/«сбой дескриптора».
  На 3/64 баг замаскирован (вся память <4 ГБ). Свойство `snps,dis-64bit-dma` в
  `usb@4e00000` (dtsi) + клэмп маски в `drivers/usb/dwc3/core.c` после блока
  `AWIDTH==64` — патч обязателен. Проверено: 3/64 работает и после фикса.
- **rootfs: `find -uid $(id -u)` ломает setuid в CI**: в `build-rootfs.sh` sweep
  владельца должен бить ТОЛЬКО по uid владельца оверлея (`stat -c %u`), не по
  `id -u` — под `sudo bash` `id -u`=0 и `chown` всего дерева срезает ВСЕ
  setuid/setgid биты (sudo/su/pkexec/dbus-helper/...). Проверка образа:
  `find / -xdev \( -perm -4000 -o -perm -2000 \) -type f` — должно быть ~23 файла.
- **icc-rpm `usb_axi` keep-alive (2026-10-17): обязателен на ОБОИХ NoC** (snoc+cnoc).
  Бисект на железе без `clk/pd_ignore_unused`: пустые intf-списки или только
  один из двух голосов → hard-hang на USB MMIO/PHY доступе (lockup→panic→ребут).
  `cpu_axi`/`ufs_axi` доказанно не нужны и сняты. ВНИМАНИЕ: тесты голосов
  под `clk_ignore_unused` бессмысленны — флаг физически держит клоки; для
  честной проверки флаги надо снимать из `kernel.config` (`CONFIG_CMDLINE_FORCE`).
- `rebuild-rescue.sh` — `zstd -19` (как build.sh); xz -9e давал заметно более
  медленный бут (kernel-side распаковка initramfs).
- **BT wedge (hci_qca, 2026-10-17):** `Frame reassembly failed (-84)` — безвредный
  boot-spew шум, НЕ признак wedge. Реальный wedge: ~1/10 power-cycle, все 4 ретрая
  с tx timeout → адаптер дерегистрируется. Восстановление БЕЗ ребута: подождать
  ~60с, потом `echo serial0-0 > /sys/bus/serial/drivers/hci_uart_qca/unbind`
  + bind. BT-рельсы общие с Wi-Fi и физически не роняются — пульсы 2400/115200
  единственный реальный power control.
