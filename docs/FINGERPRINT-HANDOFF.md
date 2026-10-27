# Handoff: сканер отпечатков FPC — старт работы

Для следующего агента. Задача: определить, возможен ли вообще сканер отпечатков
на mainline-ядре этого устройства, и если да — довести до работающего захвата.

**Текущий статус: NOT SUPPORTED.** Драйвера нет, DT-узла в mainline нет, в
`STATUS.md` строка «Сканер отпечатков | Драйвер отсутствует».

---

## 1. Что уже установлено (с доказательствами)

| Факт | Откуда |
|---|---|
| Чип — **FPC1020** (варианты A1/A2/A3/A4/A5) | строки в TA: `FPC1020 (A1)`, `FPC1020 (A2)` … |
| Android-свойство `ro.boot.fpsensor=fpc` | `getprop` на устройстве |
| HAL: `/vendor/lib64/hw/fingerprint.fpc.default.so` (+ `com.fingerprints.extension@1.0.so`) | `ls /vendor/lib64/hw` |
| **Данные идут через TrustZone**: TA `fpctzapp` в `/vendor/firmware_mnt/image/` (`.mdt` + `.b00…b07`, код в `.b02` = 2.68 МБ) | найдено на устройстве |
| TA сам делает SPI-транзакции (`%s, spi transfer failed`) | `strings` по `.b02` |
| Вендорский ядерный драйвер **не трогает SPI** — только питание/reset/IRQ через sysfs | `drivers/input/fingerprint/fpc/fpc1020_platform_tee.c` (868 строк, 4 вхождения «spi» — все про регулятор `vcc_spi`) |
| DT-узел (наш SoC, `khaje-idp.dtsi`): `compatible = "fpc,fpc1020"`, **IRQ = tlmm gpio 83**, **reset = tlmm gpio 104**, `fpc,enable-on-boot`, pinctrl `msm_gpio_104` / `msm_gpio_83` | `/mnt/931/LIneageOS/kernel_sm6225/arch/arm64/boot/dts/vendor/qcom/khaje-idp.dtsi:238` |
| В live-DT Android узел `soc/fpc1020` есть, но **SPI-шины к нему не привязано** | `/proc/device-tree/soc/fpc1020/*` |
| SPI-контроллеры на устройстве: `4a80000` = тачскрин novatek (ok), `4a84000` в стоке работает **как i2c**, `4a94000` — **disabled** | live-DT + `dmesg` стока (`i2c_geni 4a84000.i2c: …`) |
| `/sys/bus/spi/devices` на Android = только `spi0.0` (тачскрин) | проверено |
| **libfprint не поддерживает FPC** | libfprint issue #137, supported-devices |
| У FPC есть **официальный открытый драйвер** (GPL): `github.com/fingerprint-cards/capacitive_device_driver` — там `qcom/spi_driver/fpc1020_tee.c` (TEE-вариант) и не-TEE SPI-варианты | найден поиском |

Вывод из этого: **SPI-шиной владеет TEE**, а не ядро AP. Это и есть главная
неопределённость задачи.

---

## 2. Feasibility gate — сделать ПЕРВЫМ делом

Не писать драйвер, пока не ответишь на два вопроса:

**Вопрос A: какой QUP/CS использует TA?**
**Вопрос B: доступен ли этот QUP из AP (не закрыт XPU для non-secure)?**

Если B = «нет», задача на mainline нерешаема без реализации клиента к TA
(см. ветку 4B) — и это надо честно зафиксировать, а не биться в SPI.

### Как искать (по возрастанию стоимости)

1. **Конфиг TA / QSEE.** На устройстве (Android, root есть):
   ```
   find /vendor -iname "*fpctzapp*" -o -iname "*fpc*" 2>/dev/null
   ls /vendor/etc/vintf/manifest/ | grep -i finger
   ```
   Плюс посмотреть `/vendor/etc/init/android.hardware.biometrics.fingerprint-service.xiaomi.rc`.
2. **Дизасм TA.** `fpctzapp.b02` — код Hexagon; искать обращения к QUP-регистрам
   (`0x4a8xxxx`, `0x4a9xxxx`) или к «spi» сервису TEE. Инструменты: `hexagon-sdk`
   objdump либо просто поиск констант в бинаре (`grep -c` по байтам адреса).
3. **Физический замер** — самый надёжный. Снять логическим анализатором
   CLK/MOSI/MISO/CS на шлейфе сканера при разблокировке в Android:
   - какие линии шевелятся → какой QUP (сверить с `bengal-qupv3.dtsi`);
   - шевелятся ли они вообще (если да — шина живая и, скорее всего, доступна
     только secure-миру, но факт активности подтверждён).
4. **Прямая проверка из mainline.** Включить в нашем DT `spi@4a94000`
   (`qupv3_se5_spi`, пины gpio14–17) и/или `spi@4a84000` и попробовать
   одну транзакцию. Признаки XPU-блокировки: таймаут GENI, SMMU/XPU fault в
   dmesg, либо зависание. **Делать только через `fastboot boot`** (см. §4).

---

## 3. Если шина доступна (ветка 4A)

1. Взять `fpc1020.c` из официального репозитория FPC (не-TEE вариант) как основу.
2. Портировать на mainline API: `gpiod` (`devm_gpiod_get`), `regulator`,
   `input`-подсистема, `spi_driver`, `of_match_table`. Старый код использует
   `gpio_request`/`gpio_direction_*` и `spi->dev.of_node` — переписать.
3. DT-узел в `sm6225-xiaomi-fog.dts`:
   ```dts
   &qupv3_se5_spi {          /* или тот QUP, что подтвердится замером */
           status = "okay";
           fingerprint@0 {
                   compatible = "fpc,fpc1020";
                   reg = <0>;
                   spi-max-frequency = <4800000>;
                   interrupt-parent = <&tlmm>;
                   interrupts = <83 IRQ_TYPE_EDGE_RISING>;
                   reset-gpios = <&tlmm 104 GPIO_ACTIVE_LOW>;
                   fpc,enable-on-boot;
                   vdd-supply = <&pm6125_l9>;   /* проверить по стоку! */
           };
   };
   ```
   Питание сверить: в `khaje-idp.dtsi` рельсы указаны для соседнего `goodix_fp`
   (`pm6125_l9`), для FPC их в DT нет — уточнить по схеме/замеру.
4. Проверка «сенсор живой»: прочитать ID-регистр FPC1020 по SPI (в референсном
   драйвере есть `fpc1020_check_hw_id`), ожидаемый ID — из даташита/референса.
5. Userspace: **libfprint не поможет** (FPC не поддерживается). Варианты:
   - свой минимальный клиент поверх `/dev/fpc1020` (референсный драйвер отдаёт
     образы кадров через read/ioctl) — для начала достаточно сохранять кадр в
     PGM, чтобы доказать захват;
   - дальше — либо портировать матчинг (сложно), либо оставить как «сенсор
     читается, матчинг не реализован».

## 4. Если шина закрыта (ветка 4B)

Единственный путь — клиент к TA через QSEE:
- в mainline `qseecom` уже работает (UEFI secure app поднимается, см. `AGENTS.md`);
- но нужно реализовать протокол HAL↔TA (`fingerprint.fpc.default.so` ↔ `fpctzapp`),
  это большой реверс-проект;
- **оценить и, если соотношение плохое, закрыть задачу с документом** —
  «аппаратно возможно только через TEE, на mainline нецелесообразно».

---

## 5. Окружение и процедуры (обязательно к соблюдению)

Репозиторий: `/mnt/128/Redmi10c`, ветка **`v7.3.0`** (не `kernel` — она устарела).
Ядро: `out/src/linux`, сборка rescue: `tools/rebuild-rescue.sh`,
образ: `out/boot-display-console-rescue.img`.

### Правила перезагрузки (проверено, соблюдать буквально)

| Откуда | Команда |
|---|---|
| **Android** | `adb reboot bootloader` |
| **Linux/Ubuntu** | `adb shell "reboot bootloader"` |
| **Rescue-образ (initramfs, нет adb)** | на консоли: `reboot -f` (USB ACM `/dev/ttyACM0`) |

### Правило слотов (нарушение = потеря времени)

```
fastboot reboot bootloader      # 1. всегда сначала вернуться в бутлоадер
fastboot set_active b           # 2. ТОЛЬКО слот B для Linux/rescue
fastboot reboot bootloader      # 3. перечитать слот
fastboot boot out/boot-display-console-rescue.img   # 4. грузить, НЕ прошивать
```

- **Эксперименты — только `fastboot boot`, никогда `fastboot flash`.**
  Прошитый плохой образ превращает ребут-цикл в постоянный.
- Если после плохого `fastboot boot` телефон оказался в fastboot или ушёл в
  Android — сначала `fastboot reboot bootloader`, затем `set_active b`,
  затем **ещё раз** `fastboot reboot bootloader`, и только потом `boot`.
  Не забывать про шаг 2: уже дважды теряли время, оставаясь на слоте A.
- Android живёт на слоте **A** (recovery-путь), Linux — на **B**.
- Перед возвратом устройства пользователю: `set_active a` + `reboot`, чтобы
  телефон оказался в рабочем Android.

### Консоль rescue-образа

- Гаджет: USB ACM, `VID:PID 1d6b:0104`, устройство `/dev/ttyACM0`.
- Держать **постоянный читатель** на хосте ДО запуска тестов, иначе запись в
  `/dev/ttyGS0` блокируется и тест «зависает».
- Пустой лог при живом ACM = скрипт не дошёл до записи; проверять маркер
  `ACM OK` и содержимое initramfs (`zstd -dc out/build/rescue.cpio.zst | cpio -idm`).

---

## 6. Запрещено / грабли (из истории проекта)

- **Не читать `devmem` по обесточенным блокам** (CSIPHY/TLMM) — это роняло SoC
  в fastboot/ребут. Для регистров — только драйвер или debug-вывод.
- **Не дёргать GPIO через `/dev/mem`** (на NFC это дважды валило SoC).
- `v4l2-ctl --stream-mmap` без кадров уходит в D-state и через 120 с роняет ядро
  (`hung_task_panic=1`) — не оставлять такие процессы.
- Коммиты — **только** от `Damir00109`:
  `git -c user.name=Damir00109 -c user.email=internet00109@gmail.com commit`.
  Никаких `Co-Authored-By`, «Generated with» и прочих трейлеров.
- `out/` — генерируемый; правки ядра переносить в `kernel.patch` через
  `./regen-patch.sh`, иначе они потеряются.

---

## 7. Что считать результатом

Минимальный полезный итог (даже при отказе):

1. `docs/FINGERPRINT.md` — карта: чип, шина, TA, почему так, что проверено.
2. Ответ на feasibility gate с доказательством (замер / XPU-fault / дизасм TA).
3. Если шина доступна — DT-узел + драйвер + чтение ID сенсора в логе.
4. Обновлённая строка в `STATUS.md` и `docs/hardware-inventory.md` (п. 21).
5. Коммит(ы) в ветку `v7.3.0`.

## 8. Текущее состояние устройства (на момент передачи)

- Телефон **в Android на слоте A** (загружен для сбора данных по отпечатку),
  root есть (Magisk), adb работает (`adb -s 82773a69`).
- Ubuntu-rootfs на слоте B не прошит (`boot_b` затёрт), но rescue-образ грузится
  через `fastboot boot`.
- Собранный нами SSC-стек (`hexagonrpcd`, `ssccli`, `iio-sensor-proxy`) — в
  `sensors/`, см. `docs/SENSORS-SSC.md`; для отпечатка он не нужен, но показывает
  принятый в проекте стиль: собирать userspace из открытых исходников.
