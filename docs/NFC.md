# NFC — NXP SN100U (i2c1-002a, `qcom,nq-nci` на стоке)

## Итог (обновлено 2026-11-10: обнаружение карты работает)

```
~ # nfc-nci-test poll
START_POLL accepted by the kernel
polling (tap a card)...
TARGET FOUND
  target 4: proto=MIFARE  sens_res=0x0042 sel_res=0x18 nfcid1=340216290e7356
```

Тройка (MIFARE Classic) читается в mainline: UID `34 02 16 29 0E 73 56`
совпадает с тем, что показывает Android. Проверено в Ubuntu-rootfs с драйвером,
собранным модулем.

### Три бага, из-за которых раньше «ничего не находилось»

1. **`NFC_ATTR_RF_MODE` слался 4 байтами**, а в ядре это `NLA_U8`
   (`net/nfc/netlink.c`) → netlink отбрасывал всё сообщение
   (`attribute type 11 has an invalid length`), `START_POLL` до чипа не доходил.
2. **Не было подписки на multicast-группу** genl-семейства `nfc` (она называется
   `events`, а не `nfc`): события `NFC_EVENT_TARGETS_FOUND` не доставлялись,
   хотя ядро их слало (`dmesg`: `nfc: nfc_targets_found ... n_targets=1`).
3. **Событие несёт только `NFC_ATTR_DEVICE_INDEX`** — сами данные цели
   (протокол, `nfcid1`, `sel_res`) отдаются отдельным дампом
   `NFC_CMD_GET_TARGET` с `NLM_F_DUMP`; теперь инструмент его делает.

## Итог (первичный транспорт)

**Транспорт NFC в mainline работает.** 2026-11-08:

```
~ # echo 0 > /sys/class/rfkill/rfkill0/soft
~ # nfc-nci-test up
NFC_CMD_DEV_UP (CORE_RESET over I2C)...
dev_up: OK — chip answered NCI
```

`NFC_CMD_DEV_UP` прогоняет `nci_open_device()`: драйвер поднимает VEN,
шлёт `CORE_RESET_CMD` по I2C и ждёт `CORE_RESET_RSP` + `CORE_INIT_RSP`.
Успешный `dev_up` = питание, клок, I2C и IRQ в порядке. В `/proc/interrupts`
строка `111: msmgpio 70 Edge nxp-nci_i2c` с ненулевым счётчиком — чип
реально дёргает линию прерывания.

RF-обнаружение (`nfc-nci-test poll` → `NFC_CMD_START_POLL`) запускается без
ошибок; проверка на живой карте ещё не сделана (карты не было под рукой).

**Важная поправка (2026-11-09).** Раньше «START_POLL проходит без ошибок» было
неверно: ядро молча отбрасывало сообщение, потому что `NFC_ATTR_RF_MODE`
объявлен в `net/nfc/netlink.c` как **NLA_U8**, а оба инструмента отправляли его
4 байтами. В dmesg это видно как
`netlink: 'nfc-nci-test': attribute type 11 has an invalid length` (тип 11 —
`NFC_ATTR_RF_MODE`), и никакого RF-обнаружения при этом не запускалось.
Исправлено: поле отправляется одним байтом (`nfc-nci-test.c`, `nfc-read.c`).
Дополнительно `poll` теперь терпит `-EALREADY` от повторного `DEV_UP`
(раньше выходил с `Operation already in progress`) и ждёт 5 минут.

## Что оказалось важно

1. **Чип — SN100U**, не PN80T: стоковая прошивка лежит в
   `/vendor/lib/libsn100u_fw.so`, конфиг `libnfc-nxp.conf` задаёт
   `NXP_SYS_CLK_SRC_SEL=0x02` (PLL) и `NXP_SYS_CLK_FREQ_SEL=0x06` (52 МГц).
2. **Клок даёт PMIC, а не Linux.** В стоковом DT узла `nq@2a` нет ни
   `clocks`, ни регуляторов — только `qcom,nq-irq/ven/firm/clkreq`.
   `pm6125_div_clk1` (9.6 МГц) к NFC не относится. Значит PMIC генерит
   52 МГц после XBL, а CLKREQ (gpio71) только гейтит его — в mainline
   ничего поднимать не нужно, и это подтвердилось.
3. **Драйвер держит VEN в нуле до открытия устройства.** Апстримный
   `nxp-nci-i2c` на probe выставляет `enable-gpios` в 0, поэтому «чип не
   отвечает по I2C» на голой загрузке — нормальное поведение, а не
   отсутствие клока. Ранние выводы в `hardware-inventory.md` были неверны.
4. **rfkill soft-block.** `nfc0` создаётся rfkill-устройством в
   заблокированном состоянии; без `echo 0 > /sys/class/rfkill/rfkill0/soft`
   `dev_up` отдаёт `-ERFKILL`. В обычной системе это делает userspace.
5. `Read failed with error -121` (EREMOTEIO) в dmesg — единичный сбой
   чтения во время инициализации; хендшейк после него проходит.

## DT (mainline)

`sm6225-xiaomi-fog.dts`, под `&i2c2` (= `i2c@4a88000`, QUPv3 SE2):

```dts
nfc@2a {
        compatible = "nxp,nxp-nci-i2c";
        reg = <0x2a>;
        interrupt-parent = <&tlmm>;
        interrupts = <70 IRQ_TYPE_EDGE_RISING>;
        enable-gpios = <&tlmm 56 GPIO_ACTIVE_HIGH>;
        firmware-gpios = <&tlmm 31 GPIO_ACTIVE_HIGH>;
        pinctrl-names = "default";
        pinctrl-0 = <&nfc_int_default>;   /* gpio70, pull-up, input */
};
```

Стоковые состояния pinctrl для сверки: `nfc_int_active` (gpio70,
pull-up), `nfc_enable_active` (gpio56+31, pull-up),
`nfc_clk_req_active` (gpio71, pull-up). Линии VEN/FIRM нельзя ставить в
`pinctrl-0` — их забирает gpiod (конфликт claim'а, как у тачскрина).

## Инструменты

- `tools/nfc-nci-test.c` — netlink-клиент: `up` / `down` / `poll`.
  `poll` шлёт `NFC_CMD_START_POLL` (JEWEL/MIFARE/ISO14443-A/B/FELICA/
  ISO15693, initiator) и печатает `TARGET FOUND idx=… proto=… nfcid1=…`.
- `tools/nfc-probe.c` — прямое управление VEN/FIRM/CLKREQ через gpiolib
  chardev + SMBus-quick по адресу 0x2a. Нужен, когда драйвер не привязан
  (если линии заняты драйвером — деградирует до I2C-пробы).
- `tools/make-slim-rescue.sh` — воспроизводимая сборка «тонкого» rescue
  (13.9 МБ, влезает в `fastboot boot`); кладёт оба инструмента в initramfs.

## Стоковые артефакты (локально, не в git)

`out/nfc-recon/android/`: `libsn100u_fw.so` (прошивка),
`libnfc-nxp.conf`, `libnfc-nci.conf`, `init.fog.nfc.rc`.
Исходник вендорного драйвера: `docs/nfc/stock/nq-nci.c` (+ `.h`) из
`iDeadXD/ai-kernel_xiaomi_fog-msm4.19`.

## Драйвер собирается модулем (для быстрых итераций)

`CONFIG_NFC_NXP_NCI=m` + `CONFIG_NFC_NXP_NCI_I2C=m`: `rebuild-rescue.sh` кладёт
`nxp-nci.ko` / `nxp-nci_i2c.ko` в `/lib/modules` образа. Перезагрузка модуля
(`rmmod` + `insmod`) даёт чипу чистый цикл VEN и новую NCI-сессию — то, чего не
даёт повторный `DEV_DOWN`/`DEV_UP` (после него чип отвечает `-121`/таймаутом).
После `insmod` rfkill-индекс может измениться: NFC — не всегда `rfkill0`,
проверять `cat /sys/class/rfkill/rfkill*/name`.

`nfc-nci-test poll` теперь печатает вердикт ядра (`START_POLL accepted by the
kernel` / `rejected: <errno>`) и поддерживает сужение маски: `poll iso`
(только ISO14443-A), `poll mifare`.

## Контрольный тест в Android (2026-11-10)

Антенна и карта проверены стоком: приложение Mifare Classic Tool (APK с GitHub,
`de.syss.MifareClassicTool`) видит Тройку без каких-либо действий —
`UID: 34 02 16 29 0E 73 56` (7 байт). Значит железо в порядке, и проблема
mainline-стека не в антенне и не в карте.

**Важно:** на хосте не должно быть запущено UltiMaker-Cura — она
автоподключается к `/dev/ttyACM*` и забивает консоль rescue G-кодом `M105`,
после чего ввод в консоль не доходит.

## Дальше

1. Проверить RF-обнаружение на реальной карте (`nfc-nci-test poll`,
   `nfc-read`; карта МИФАР Классик «Тройка» есть — сток её видит).
2. Userspace-стек: **neard собран и установлен в rootfs-образ** (2026-11-11).
   В Ubuntu resolute/Debian neard из архивов убрали — собирается из
   `https://git.kernel.org/pub/scm/network/nfc/neard.git` (meson, systemd=disabled).
   Установка: `/usr/local/libexec/nfc/neard`, юнит `neard.service` (enabled),
   D-Bus activation `org.neard.service`, конфиг `/usr/local/etc/neard/main.conf`
   (`ConstantPoll=true`, `DefaultPowered=true` — адаптер сам включится и будет
   поллить). Тест-утилита `nfctool` в `/usr/local/bin`. Скрипт:
   `rootfs/tools/build-neard.sh` (вызывается из `build-rootfs.sh`).
   Внимание: `libnfc-bin`/`nfc-list` НЕ работают с ядерным NCI-стеком —
   libnfc говорит с чипом напрямую, а не через netlink; для проверки использовать
   `nfctool`/`neard` или наши `nfc-*` утилиты.
3. Прошивка чипа (`NFC_CMD_FW_DOWNLOAD`, blob `libsn100u_fw.so`) —
   только если понадобится для карт/платежей (MIFARE Classic/Type 2 работают
   на ROM-NCI без прошивки).

## Статус 2026-11-12: neard end-to-end + init-последовательность SN100U

**Работает:** neard 0.20 + D-Bus — карта «Москвёнок» (MIFARE Classic,
UID `1d ba 06 1b 01 00 03`, 7 байт) детектится как `Type 2`/`MIFARE`,
`ReadOnly: false`, Tag-объекты `/org/neard/nfc0/tag0`, авто-poweron
через `nfc-poweron.sh` (ExecStartPost neard.service).

**Сырой обмен (AF_NFC) — в отладке:** AUTH_KEY_A и Type2 READ уходят
в чип (`00 00 LL ...` data-пакеты), но чип отвечает только data-status
`60 06 03 01 00 01` без полезной нагрузки. `nfc-mfread` переписан:
отдельный чистый netlink-сокет для GET_TARGET (как в nfc-nci-test),
учёт 7-байтного UID (Classic auth требует NUID = uid[0..3]).

**Init-последовательность (важно):** стоковый Android после
CORE_INIT шлёт ~27 команд (PROP_ACT `2f 02 00`, десятки CORE_SET_CONFIG
включая 4×~250-байтных RF-блока, TVDD_CFG_1, CORE_CONF/EXTN,
GET_CONFIG, затем повторные RESET+INIT). Всё это снято из logcat
`NxpNciX` и воспроизводится дословно в `nxp_nci_post_setup()`
(`nxp_sn100u_init_seq[]` в drivers/nfc/nxp-nci/core.c, параметр
`seq_enable=1`). Без этих конфигов чип находит карту, но data-exchange
не возвращает данные — вероятная причина текущего блокера.

**Дедлок remove (исправлен):** `nxp_nci_remove()` держал
`info->info_lock` поверх `nci_unregister_device()` →
`nci_close_device()` → `nxp_nci_close()` берёт тот же info_lock
(не рекурсивный) → взаимная блокировка с IRQ-потоком, rmmod вис
навсегда. Фикс: info_lock отпускается до unregister; в
`nxp_nci_i2c_remove` `free_irq()` теперь ДО `nxp_nci_remove()`.

**Грабля с модулями:** в rootfs ДВА пути .ko —
`/lib/modules/7.3.0-dirty/nxp-nci.ko` (верхний уровень, фактически
загружается) и `.../kernel/drivers/nfc/nxp-nci/`. Обновлять оба!
`modules_install` кладёт только второй — верхний копировать руками.

## Статус 2026-11-14: бисект seq, найден убийца — большие I2C-транзакции

**Ночной стенд:** `boot_b` = slim-rescue (ядро+модули в initramfs, NFC
не автозапускает), краш → watchdog/panic → авторебут в тот же rescue.
Модули заливаются в `/tmp` по serial (gzip+base64 чанками ≤800 байт —
лимит строки tty ~1024). pstore: `console-ramoops-0` (plain text) +
`dmesg-ramoops-0.enc.z` (raw deflate, на устройстве не распаковывается —
CRYPTO_DEFLATE=m нет в initramfs; снимать b64 и жать `zlib -15`).

**Результаты бисекта (seq_max = сколько SET_CONFIG кадров шлём):**
- `seq_enable=0`: dev_up/down/rmmod — чисто, дедлок remove побеждён.
- seq_max 1..6 (кадры ≤18 байт TX): все OK.
- **seq_max=7 = первый 250-байтный RF-конфиг (TX 253 байта) → мгновенный
  сброс SoC без panic-лога** (console-ramoops обрывается прямо на
  `tx 253 bytes`). Похоже на зависание QCOM i2c → WDT-reset.

**Фикс в i2c.c:** TX-кадры >32 байт бьются на i2c_msg по 32 байта через
`i2c_transfer` с `I2C_M_NOSTART` — для чипа это один непрерывный
транзакт. Проверка показала: **сброс всё равно происходит** — значит
проблема не в размере одной i2c_msg, а либо в суммарной длине
непрерывного transfer, либо в содержимом кадра (RF-конфиг включает
что-то, что роняет железо/питание).

**Гипотезы дальше:** (а) дробить с STOP между чанками и паузами —
проверить, примет ли чип; (б) кадр 7 может быть RF-enable с неверным
TVDD → brown-out PMIC; (в) возможно Android шлёт эти кадры иначе
(фрагментация на уровне NCI). До выяснения `seq_enable=0` — рабочий
безопасный режим (детект карты и neard работают и без реплея).
