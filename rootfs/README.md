# rootfs-ubuntu — сборка Ubuntu Server 26.04.1 (resolute) для Redmi 10C

Независимая папка для сборки rootfs, прошиваемого на раздел `cust`
(Xiaomi Redmi 10C `fog`/`rain`, SM6225). Ядро грузится отдельно через
`boot-linux.img` с `tools/pivot-init` (mount `PARTLABEL=cust` → switch_root).

## Структура

| Путь | Назначение |
|---|---|
| `build-rootfs.sh` | Главный скрипт: extract → chroot apt → overlay → ext4 → sparse |
| `cache/` | `ubuntu-26.04-server-cloudimg-arm64-root.tar.xz` (уже скачан, ~241 МБ, sha256 `6ed455eb…`) |
| `tools/rain-overlay/` | Canonical overlay из ветки `main` репо (111 файлов): Wi-Fi/modem/BT/audio скрипты + systemd units |
| `tools/pivot-init` | Initramfs-init: USB PHY + ACM gadget → mount cust → switch_root |
| `tools/pack-ubuntu-boot.sh` | Упаковка `boot-linux.img` (ядро + slim pivot initramfs) |
| `tools/build-ubuntu-dualboot.sh` | Старый dualboot-скрипт (debootstrap-путь, для справки) |
| `tools/twrp-update-binary-ubuntu` | Упаковка TWRP sideload zip |
| `out/` | `rootfs-26.04/` (дерево), `linux_rootfs.img` + `.sparse.img` |
| `logs/` | Логи apt-chroot и сборки |

## Использование

```bash
./build-rootfs.sh                 # полный цикл → out/linux_rootfs.sparse.img
IMG_SIZE_MB=1900 ./build-rootfs.sh  # если нужен другой размер (cust = 2 GiB)
REDMI=/path/to/Redmi10c ./build-rootfs.sh  # другой путь к чекауту ядра

# прошивка
fastboot flash cust out/linux_rootfs.sparse.img && fastboot continue
```

Опциональные источники из чекаута `$REDMI` (по умолчанию `/mnt/128/Redmi10c`):
- kernel modules: `out/build/linux-7.1.5/lib/modules/` — если пусто, скрипт
  печатает команды `modules`/`modules_install` и продолжает без них
- firmware: `initramfs/lib/firmware/` → `/lib/firmware` в rootfs
- `vendor-qcom/` → `/usr/share/vendor-qcom` (hexagonrpcd -R tree)
- `initramfs/{tqftpserv,qrtr-ns,pd-mapper,rmtfs,qrtr-lookup}` + `out/build/busybox`

## Окружение хоста (проверено)

- x86_64, binfmt `qemu-aarch64` с флагом `F` — chroot в arm64 работает без
  копирования qemu внутрь rootfs.
- Нужны: `debootstrap`(опц.), `rsync`, `mkfs.ext4`, `img2simg`, `sudo` без пароля.

## Важно: systemd vs OpenRC

Этот скрипт строит rootfs на **systemd** (как рабочая 26.04-сборка с
`main`-ветки). На устройстве сейчас по STATUS.md работает **OpenRC**
(владелец ModemManager/polkit, загрузка ~34 с). OpenRC-overlay в git нет —
жил на старом диске. Варианты восстановления:

1. Слить с устройства: загрузить текущее ядро, смонтировать `cust`,
   скопировать `/etc/init.d`, `/etc/conf.d`, openrc-сервисы.
2. Старый рабочий rootfs 24.04 лежит в релизе `v2026.09.06-final`
   (`linux_rootfs-24.04-new.sparse.img`) — источник сервисов/скриптов.

## Связанные куски

- Клон репо/ядро: `/mnt/128/Redmi10c` (ветка `kernel`)
- Rescue-образ: `/mnt/128/Redmi10c/out/boot-display-console-rescue.img`
- Статус железа: `/mnt/128/Redmi10c/STATUS.md`
