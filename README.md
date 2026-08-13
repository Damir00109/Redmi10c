# Redmi 10C (rain/fog) — mainline Linux bring-up

Проект по портированию **mainline Linux (7.1.y)** на Xiaomi Redmi 10C
(codename `rain`, вариант `fog`, SoC Qualcomm SM6225 "Khaje") с загрузкой в
Ubuntu 24.04 через кастомный `initramfs` (`pivot-init` → `switch_root`),
без замены Android — используется свободный слот `b` (A/B) и партиция
`cust` под rootfs.

**Текущий статус:** ядро полностью грузится, `switch_root` в Ubuntu 24.04
проходит успешно (systemd, adb по USB-гаджету). Подробный статус — в
[`AGENTS.md`](AGENTS.md) и [`STATUS.md`](STATUS.md). Экран сейчас работает
только через `simple-framebuffer` (текстовая консоль), полноценный DRM/DSI-
драйвер панели временно отключён (вызывал зависания при загрузке) — исходники
драйвера сохранены в `archive/display-bringup-disabled-20260813/` для
последующей доработки.

Это репозиторий с **исходниками и патчами**, а не с готовыми бинарниками.
Ниже — полная инструкция, как всё собрать с нуля.

---

## Структура репозитория

| Путь | Назначение |
|---|---|
| `patches/redmi10c-mainline-*.patch` | Патч поверх `git.kernel.org/.../linux.git` (тег `v7.1.5`) со всеми правками под Redmi 10C |
| `configs/rain-fog-working.config` | Рабочий `.config` ядра (уже прошедший полную загрузку в Ubuntu) |
| `configs/busybox-1.36.1.config` | `.config` для busybox, используемого в `pivot-init` |
| `tools/pivot-init` | Init-скрипт initramfs: поднимает USB ACM, монтирует `cust`, делает `switch_root` |
| `tools/pack-ubuntu-boot.sh` | Собирает `boot-linux.img` (ядро + initramfs + dtb) |
| `tools/build-ubuntu-dualboot.sh` | Собирает rootfs Ubuntu (`linux_rootfs*.img`) через debootstrap |
| `tools/restore-android.sh` | Откат слота `a` на стоковый Android/LineageOS |
| `AGENTS.md` | Технический журнал: что именно ломалось и как было исправлено (для ИИ-агентов и людей) |
| `STATUS.md`, `PLAN.md`, `HARDWARE-DRIVERS.md` | История портирования по подсистемам |
| `archive/` | Снэпшоты успешных сборок (не для продолжения работы, только бэкап) |

---

## Требования к хосту (Ubuntu/Debian)

```sh
sudo apt install \
  gcc-aarch64-linux-gnu g++-aarch64-linux-gnu binutils-aarch64-linux-gnu \
  device-tree-compiler cpio python3 git \
  android-sdk-libsparse-utils \
  adb fastboot
```

Кросс-компилятор должен давать префикс `aarch64-linux-gnu-` (проверено на
GCC 15.2.0, но подойдёт большинство современных версий).

---

## Шаг 1 — Ядро: клонировать, применить патч, собрать

```sh
# 1.1 Клонировать mainline linux-stable на нужном теге
git clone --branch v7.1.5 --depth 1 \
  https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git \
  mainline/linux-7.1.y
cd mainline/linux-7.1.y

# 1.2 Применить патч Redmi 10C (добавляет DTS, панель, pinctrl-khaje,
#     правки DSI/UFS/USB/ath10k и т.д.)
git apply --stat ../../patches/redmi10c-mainline-20260813.patch   # предпросмотр
git apply ../../patches/redmi10c-mainline-20260813.patch

# 1.3 Взять готовый рабочий .config
mkdir -p ../../out/linux-7.1.5
cp ../../configs/rain-fog-working.config ../../out/linux-7.1.5/.config

# 1.4 Собрать
cd ../..
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -j"$(nproc)" \
  -C mainline/linux-7.1.y O="$PWD/out/linux-7.1.5" \
  olddefconfig Image.gz dtbs
```

Результат: `out/linux-7.1.5/arch/arm64/boot/Image.gz` и
`out/linux-7.1.5/arch/arm64/boot/dts/qcom/sm6225-xiaomi-fog.dtb`.

> Если после `olddefconfig` какая-то опция "отвалилась" (например,
> `CONFIG_FRAMEBUFFER_CONSOLE` или `CONFIG_DRM_MSM` превратился в модуль),
> верните её явно:
> `mainline/linux-7.1.y/scripts/config --file out/linux-7.1.5/.config --enable ИМЯ_ОПЦИИ`
> и пересоберите. Список критичных опций — в `AGENTS.md`.

---

## Шаг 2 — busybox для initramfs (статическая сборка под aarch64)

```sh
git clone --branch 1_36_1 https://github.com/mirror/busybox.git tools/busybox-src
cd tools/busybox-src
cp ../../configs/busybox-1.36.1.config .config
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -j"$(nproc)" oldconfig busybox
mkdir -p ../../out/initramfs-root/bin
cp busybox ../../out/initramfs-root/bin/busybox
cd ../..
```

Проверьте, что бинарник статический и под aarch64:
`file out/initramfs-root/bin/busybox` → `ELF 64-bit ... ARM aarch64 ...
statically linked`.

---

## Шаг 3 — mkbootimg (AOSP-инструмент для упаковки boot-образа)

```sh
git clone https://android.googlesource.com/platform/system/tools/mkbootimg \
  tools/mkbootimg
```

(В инструкции ниже используется именно `tools/mkbootimg/mkbootimg.py` —
версия из системных пакетов Ubuntu (`/usr/bin/mkbootimg`) может не
поддерживать нужные опции `--header_version 2`.)

---

## Шаг 4 — собрать boot-linux.img

```sh
bash tools/pack-ubuntu-boot.sh
```

Скрипт:
1. Берёт `out/initramfs-root/bin/busybox`, раскладывает относительные
   симлинки на все аплеты (`ls`, `mount`, `switch_root`, …).
2. Копирует `tools/pivot-init` как `/init` initramfs.
3. Паковкует `pivot.cpio.gz`.
4. Через `mkbootimg.py` (header v2) собирает `out/ubuntu-dualboot/boot-linux.img`
   из `Image.gz` + `pivot.cpio.gz` + `sm6225-xiaomi-fog.dtb`.

Результат должен быть ~17 МиБ.

---

## Шаг 5 — rootfs Ubuntu для партиции `cust`

Полный debootstrap-пайплайн — в `tools/build-ubuntu-dualboot.sh` (требует
`sudo`, `debootstrap`, `qemu-user-static` для aarch64). Он создаёт
`out/ubuntu-dualboot/linux_rootfs.img` (ext4) и sparse-версию для fastboot.

**Важно** (найденный и исправленный баг, см. `AGENTS.md`): rootfs должен
иметь либо настоящий merged-usr (`/lib -> usr/lib`), либо хотя бы символьную
ссылку `/lib/systemd -> ../usr/lib/systemd`, иначе `/sbin/init ->
../lib/systemd/systemd` не резолвится и `switch_root` проваливается с
`ENOENT` уже ПОСЛЕ удаления initramfs (безвозвратно вешает шелл до
физической перезагрузки). Проверить после сборки:

```sh
sudo mount -o loop <linux_rootfs.img> /mnt
readlink -f /mnt/sbin/init   # должно резолвиться в существующий файл
sudo umount /mnt
```

---

## Шаг 6 — прошивка (только слот `b`, слот `a` не трогать!)

Слот `a` держим как рабочий Android/LineageOS fallback с включённой
верификацией (AVB). Слот `b` — для тестов mainline, с кастомным
`vbmeta_b` (verification отключена, флаг `--flags 2` в `avbtool`) и
без `dtbo_b` (заполненный `dtbo` вызывал зависания загрузки).

```sh
fastboot erase dtbo_b
fastboot flash boot_b out/ubuntu-dualboot/boot-linux.img
fastboot flash cust   out/ubuntu-dualboot/linux_rootfs.sparse.img   # или ...-sparse.img
fastboot --set-active=b
fastboot reboot
```

Если `fastboot boot`/`flash` вернёт
`FAILED (remote: 'Failed to load/authenticate boot image: Load Error')`,
это значит телефон в "псевдо-fastboot" после предыдущей неудачной попытки —
сделайте `fastboot reboot` и повторите команду (подробности в `AGENTS.md`).

Проверка после загрузки (USB ACM/adb должны подняться сами):

```sh
adb devices           # ожидаем "rain    device"
adb shell uname -a     # 7.1.5-dirty ...
adb shell cat /etc/os-release   # Ubuntu 24.04 LTS
```

Откат на Android (слот `a`):

```sh
fastboot --set-active=a
fastboot reboot
```

---

## Известные проблемы / что дальше

См. таблицу статусов и детальный технический журнал в `AGENTS.md` и
`STATUS.md`. Кратко:

- DRM/DSI-панель отключена (была причиной зависаний), сейчас только
  simplefb-консоль.
- Wi-Fi (`ath10k`) требует модулей ядра, собранных именно под текущую
  версию `Image.gz` — старые `.ko` из прежних сборок не грузятся.
- Изредка происходит самопроизвольная перезагрузка при бринг-апе
  Wi-Fi/модема (soft-hang, не связано с изменениями из этого README).

---

## Безопасность / договорённости с владельцем устройства

- **Верификацию (`dm-verity`/AVB) на слоте `a` не отключать** — это
  единственный гарантированно рабочий fallback.
- Разрушительные операции (erase/flash системных партиций, смена
  `vbmeta`) выполняются только на слоте `b` или на явно указанных
  тестовых партициях (`cust`).
