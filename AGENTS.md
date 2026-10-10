# AGENTS.md — заметки для агента

## Окружение

- Рабочий диск: `/mnt/128` (110 ГБ свободно, выделен под проект). CWD — `/mnt/128/Redmi10c`.
- `sudo` без пароля. `gh` залогинен как `Damir00109` (ssh). Git user.name/user.email не настроены.
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
- `tools/rebuild-rescue.sh` — быстрая пересборка в уже настроенном дереве (вНИМАНИЕ: пакует initramfs через `xz -9e`, а не zstd — возможно устарело).
- `tools/boot-and-check.sh` — загрузка + проверка.
- `regen-patch.sh` — перегенерация `kernel.patch` из дерева `out/src/linux`.
- Загрузка на устройство: `fastboot set_active b && fastboot reboot bootloader`, затем `fastboot boot out/boot-display-console-rescue.img` (слот B, ничего не прошивается; Android цел на слоте A; rootfs — Ubuntu на разделе `cust`). Лимит `fastboot boot` ~57.5 МиБ.
- Консоль на устройстве: USB gadget ACM → `screen /dev/ttyACM0 115200` (иногда нужен Ctrl-C для prompt'а), плюс shell на экране.

## Статус железа (STATUS.md, обновление 2026-10-06)

Работает: экран DSI, USB gadget, UFS+SD, Wi-Fi (ath10k/WCN3990), BT+A2DP, тачскрин FT8006S (SPI), LTE через IPA v4.2/QMAP (~9 Мбит/с), модем+QMI/ModemManager, звук (динамик + гарнитура FSA4480), зарядка SMB1351 + fuel gauge, thermal, cpufreq, GPU Adreno 610 (SMMU/GMU/ZAP, devfreq), сенсоры через SSC/hexagonrpcd, Plasma Mobile + Firefox, вибратор, фонарик, RTC.

В разработке: GPS, iio-sensor-proxy, ACDB, голос/SMS. Не работает: камера, сканер отпечатков.

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
- **Сеть = NetworkManager**: NM управляет ВСЕМ (wlan0 + qrtr0-модем). Профили — netplan: `/etc/netplan/90-NM-wifi.yaml` (Xiaomi_E4C4+psk), `90-NM-tele2.yaml` (APN internet.tele2.ru). `rmnet_ipa0` unmanaged (сырой транспорт). NM+ath10k зависаний НЕТ — ранние hang'и были артефактом UFS-коррупции. QMI: `qmicli -d qrtr://0`. SIM требует разового бинда: USIM app висит в "detected" → `--uim-change-provisioning-session session-type=primary-gw-provisioning,activate=yes,slot=N,aid=<AID>` (сохраняется в EFS). Quirk: MM 1.25.95 SEGV если `qmapmux0.0` уже есть при коннекте — удалять линк. ModemManager/NM НЕ маскировать.
- **qseecom allowlist**: правка в `out/src/linux/drivers/firmware/qcom/qcom_scm.c` (не в патче пока — `regen-patch.sh` при следующей регенерации).
- Известные мелочи: `hci0 Frame reassembly failed` и `ath10k invalid board magic` — безвредный шум; `fastboot`+`upower`+`fake-hwclock` поставлены в rootfs.

## Грабли (важно)

- `QCOM_CLK_SMD_RPM` и `SM_GPUCC_6115` обязательны в конфиге — без них всё висит на EPROBE_DEFER.
- Ноде вибратора (gpio36) нельзя давать pinctrl — gpiod/pinctrl дерутся за пин.
- devmem-поки по TLMM на этом чипе вешают шину — использовать `vibtest` (EV_FF ioctl).
- Магнитометра/компаса в железе нет. devinfo slot byte не работает — слоты переключаются через GPT type GUID + UFS boot LUN.
