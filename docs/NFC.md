# NFC — Redmi 10C (SN100U / PN557, NXP NCI 2.0)

Состояние на 2026-11-15. Контроллер: NXP SN100U (PN557-class), I2C `3-002a`,
NCI 2.0, проприетарный HAL Android: `nfc_nci_nxp.so` + `libsn100u_fw.so`.

## Железо / DT

`nfc@2a` в `sm6225-xiaomi-fog.dts`:
- `compatible = "nxp,nxp-nci-i2c"`, `reg = <0x2a>`
- IRQ: GPIO70, rising edge (`enable-gpios` = GPIO56 VEN, `firmware-gpios` = GPIO31 FIRM)
- GPIO71 (CLKREQ) пока не заведён.

Android-маппинг из vendor-конфигов: FIRM=31, VEN=56, IRQ=70, CLKREQ=71.

## Что работает

- `nxp-nci` + `nxp-nci_i2c` пробятся, `nfc0` регистрируется.
- `neard` (собран из исходников в rootfs) стартует, снимает rfkill,
  power on + poll — **детект карты работает**: MIFARE Classic 1K,
  UID `340216290e7356`.
- `tools/nfc-mfread.c` / `/tmp/nfc-raw.c` (static aarch64): netlink
  `START_POLL`/`GET_TARGET` + AF_NFC raw socket.

## Что НЕ работает

- `DATA_EXCHANGE` по MIFARE: auth/read → `errno=110 (ETIMEDOUT)`.
  Ядро шлёт DATA_PKT, контроллер отвечает только credits NTF
  (`60 06 03 01 00 01`), DATA_PKT RX не приходит.

## Ключевая находка: SN100U auto-activation

SN100U **сам активирует** найденную цель сразу после discovery —
приходит unsolicited `61 05` activation NTF с `iface=0x01/proto=0x02`
(стандартный T2T/FRAME). Host select не отправляется, ядро видит
state `NCI_POLL_ACTIVE` и пропускает `RF_DISCOVER_SELECT`.

На Android target НЕ auto-activate: HAL шлёт
`RF_DISCOVER_SELECT = 21 04 03 01 80 80` → `41 04` rsp →
activation NTF `61 05 18 01 80 80 ...` — **iface/proto = 0x80/0x80**
(NXP proprietary MIFARE interface).

Вывод: на iface 0x01 data path для MIFARE Classic мёртв —
контроллер молчит на любые кадры (`60 blk key`, `60 blk nuid key`,
`30 blk`). Нужна активация через proprietary 0x80.

## Изменения в ядре (kernel.patch, см. docs/nfc/nxp-nci-sn100u-local.patch)

1. `net/nfc/nci/core.c` — `nci_activate_target()`: слать
   `RF_DISCOVER_SELECT` также в state `NCI_POLL_ACTIVE`, если у
   драйвера есть `ops->rf_select_iface` (opt-in, только NXP).
   Позволяет переактивировать auto-activated target с proto 0x80 —
   Android делает ровно это (спамит select каждые ~140 мс).

2. `drivers/nfc/nxp-nci/core.c`:
   - `nxp_nci_rf_select_iface()` — для T2T подменяет
     `rf_protocol`/`rf_interface` на `0x80` (param `mfc_raw`, default 1).
   - `nxp_sn100u_init_seq[]` — реплей 16 SET_CONFIG фреймов из
     Android-лога инициализации (FW 12.1.16). Шлётся один раз через
     delayed work после первого `post_setup`, только когда
     `state == NCI_IDLE`, с защитой `seq_done` (не повторяется на
     каждый open — иначе SET_CONFIG ломает активный RF-поллинг,
     `dev_up` → `-EPROTO`). Param `seq_enable`, default 1.

3. `drivers/nfc/nxp-nci/i2c.c` — debug-принты tx/rx кадров
   (включаются через dynamic_debug).

4. `include/net/nfc/nci_core.h` — `ops->prop_ops/n_prop_ops/
   core_ops/n_core_ops` (нужны для `nci_prop_cmd`/`nci_core_cmd`,
   используются для PROP_ACT `2f 02` и реплея seq).

## Userspace-обёртка (SN100 MIFARE, по реверсу NxpMfcReader)

`NxpMfcReader::BuildMfcCmd` в `nfc_nci_nxp.so` заворачивает
MIFARE-команды в NXP proprietary формат:
- auth: wrapper cmd `0x40` (sector, flags, key)
- read: wrapper cmd `0x10` + mifare frame
- ответы: auth → `0x40`, read done → `0x10 0x0a`

Реализовано в `/tmp/nfc-raw.c` как `sn100_auth()`/`sn100_read()`,
режим `troika` — полная таблица ключей Troika (16 секторов).

## Отладка / ловушки

- **`nci_allocate_device: Too many proprietary commands: <мусор>`** —
  probe fail -12. Причина: на rootfs лежали **две** копии модулей —
  `/lib/modules/7.3.0-dirty/*.ko` (старые, реально грузятся modprobe)
  и `.../kernel/drivers/nfc/nxp-nci/*.ko`. Обновлять ОБЕ + `depmod -a`.
- `neard` держит устройство — для `nfc-raw` делать `systemctl stop neard`.
- `fastboot boot` работает только при `current-slot: b`
  (при slot a — `Load Error` → fallback в Android).
- Эксперименты — ТОЛЬКО `fastboot boot`, ничего не шить.
- `/tmp/nfc-raw.c` — рабочая копия тулзы; `tools/nfc-mfread.c`
  в репо — старая версия (IDE затирала буфером, переносить
  осознанно).

## Автотест (ночной режим)

На устройстве: `nfc-autotest.service` (enabled) →
`/root/nfc-autotest.sh` → цикл `timeout 600 /data/local/tmp/nfc-raw troika`,
лог `/var/log/nfc-autotest.log` (cust-раздел, переживает ребут).
Включает dynamic_debug, пишет dmesg до/после.

## Следующие шаги

1. Проверить с картой: select `01 80 80` → activation NTF `01 80 80`
   → sn100 auth (`40 ...`) → read (`10 ...`).
2. Если select в POLL_ACTIVE не срабатывает (чип NAK) — смотреть
   rsp `41 04` status и пробовать deactivate+select.
3. Уточнить формат `40`-wrapper по Android-трейсу (`NxpMfcReader::
   BuildAuthCmd`) — возможно нужен ещё extended command `0x1003`.
4. CLKREQ GPIO71 — если отклики будут нестабильны.
5. После рабочего чтения — убрать debug-принты, решить судьбу
   seq-replay (минимальный набор конфига?), обновить neard.
