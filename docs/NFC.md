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

## РАБОЧЕЕ ЧТЕНИЕ MIFARE (2026-11-15) — DONE

Полный дамп Troika/MIFARE Classic 1K (UID `1dba061b010003`) прочитан
под mainline Linux. Все 16 секторов, все 64 блока.
Дамп: `docs/nfc/mifare-dump-linux.txt`, тулза: `tools/nfc-mfrw.c`.

Финальная рабочая последовательность:

1. `nxp-nci` реплеит Android SN100U init (16 конфиг-кадров, без `0x21`
   RF-mgmt — они ломали MAP) + `pre_discover` (2× prop-only discover
   `21 03 05 02 80 01 81 01`, deactivate `21 06`, poll-config `21 01 60`,
   set-config `20 02 17 ...`, `20 02 07`) + `post_discover`
   (`20 09 01 00`, `20 02 04 01 02 01 01`).
2. Discover — штатный 7-tech `21 03 0f` (prop-techs 0x80/0x81 уже
   добавлены в nci core при MIFARE в маске).
3. Карта auto-activate `61 05 ... 01 01 02` → userspace connect →
   kernel `nci_activate_target`: `21 06 01 01` (deactivate-sleep) →
   `21 04 03 01 80 80` (select, **select надо слать дважды** — первый
   отвечает `60 07 01 a1`, второй → `61 05 ... 01 80 80`).
   Клиент ретраит connect() — второй connect шлёт select повторно.
4. Userspace: AF_NFC `SOCK_SEQPACKET` (НЕ SOCK_RAW — у него
   `sock_no_connect`!), `sockaddr_nfc.nfc_protocol = NFC_PROTO_MIFARE(2)`.
   netlink: подписка на genl multicast "events" обязательна
   (`nl_groups=0xffffffff`), TARGETS_FOUND несёт только DEVICE_INDEX —
   детали цели через `NFC_CMD_GET_TARGET` dump.
5. Протокол SN100 prop-обёртки (payload без NCI-заголовка):
   - auth sector S keyA: `40 <sector> 10 <key6>` → ответ `00 40`
   - auth keyB: `40 <sector> 11 <key6>` (предположительно)
   - read block B: `10 30 <block>` → ответ `00 10 <16 байт данных>`
   - NACK → SN100 шлёт ~150-220 байт TLV-ошибки и рвёт линк.

Все 16 секторов Troika открываются ключом A `a0a1a2a3a4a5`.
Содержимое: блоки 0-2 — manufacturer/data, сектор 1 (блоки 4-5) —
данные билета (`50c70d1a...`, `f7bc81d2...`), остальное нули.
