# План портирования отпечатка Silead на mainline

Составлен 2026-11-06 по результатам исследования стокового Android 13
(`rain_global` V14.0.9.0.TGEMIXM, слот A, Magisk root, adb).

## 0. Что подтверждено на живом стоке

- Железо и стек идентичны тому, что мы уже реверсили: HAL
  `fingerprint.silead.default.so` → `libsl_fp_impl.so` → QSEECom → TA
  `sileadta` (образы в `/vendor/firmware_mnt/image/`, копии в
  `out/fp-recon/`). Сенсор живой: chip id `0x6135a080`.
- **Рабочий захват кадра уже существует**: `/mnt/128/silead-snap/`
  (`libsileadrun.so` + `snap.sh` + `src/native-lib.cpp`, собрано 2026-11-04).
  Это прямой QSEECom-клиент, который повторяет вендорский auth-путь и
  выгружает реальный кадр 64×64 через `0x52`. Эталонная последовательность —
  в `native-lib.cpp`, функция `main()` режим `once`.
- Ядерный драйвер `/dev/silead_fp` — `sil,silead_fp`, платформа `0.silead_fp`.
  **Исходники публичны**: `drivers/input/fingerprint/silead/` в
  `iDeadXD/ai-kernel_xiaomi_sm6225` (тот же SoC) — `silead_fp.c` (char-dev,
  ioctl, netlink, uinput) + `silead_fp_qcom.c` (power/reset/IRQ/pinctrl).
  Копии для справки: `/tmp/silead_fp.{c,h}`, `/tmp/silead_fp_qcom.c`
  (временные; при необходимости положить в `docs/fingerprint/stock/`).
- DT-узел (живой `/proc/device-tree/soc/silead_fp`, совпадает с тем, что
  мы добавили в patch): `avdd-gpios`=gpio100, `rst-gpios`=gpio104,
  `irq-gpios`=gpio83 (flags `0x2008`), `interrupts`=<83, 0x2008>,
  `qcom,qup-id`=6, `spi-max-frequency`=4.8 МГц, `avdd-supply`/`vddio-supply`
  = pm6125_l9 (phandle 0x6d), `regulator-always-on`.
- База на стоке **пустая**: `dumpsys fingerprint` → `count:0`, `silead.tpl`
  (980 Б) — заголовок + нули. Раньше заенролленные `.dat`
  (`fff0cad64`, `f7f0961aa`) у нас уже есть в `out/fp-recon/android/fpdata/`
  и initramfs `out/build/initramfs/lib/firmware/fp/`.
- Снятые артефакты: `docs/fingerprint/stock/silead.tpl`, `silead.bak`,
  `silead.cfg` (19993 Б — дамп конфига/калибровки TA, совпадает с ответом
  `0x59(0xf000,0x1004)`).

## 1. Модель вендорского стека (точная)

```
Android HAL (libsl_fp_impl.so)
  → /dev/silead_fp ioctls: INIT, RESET, ENABLE_IRQ, WAIT_IRQ, CLR_IRQ
  → libQSEEComAPI → /dev/qseecom → TZ → sileadta → QUP SPI → чип
```

Ключевые факты из исходников драйвера:

- Драйвер — **только обвязка**: AVDD/VDDIO (regulator + gpio100), reset
  gpio104 (low 5 мс → high, пауза ~3 мс), IRQ gpio83.
- IRQ запрашивается `request_irq(..., IRQ_TYPE_EDGE_RISING, "silfp")` и
  **сразу disable** (`disable_irq_nosync`). Включается только по
  `SIFP_IOC_ENABLE_IRQ` (11) — с этого момента каждый фронт завершает
  `completion done` и шлёт netlink-сообщение `SIFP_NETLINK_IRQ` HAL'у.
- `SIFP_IOC_RESET` (10): `irq_ignore=1` → (если IRQ включён — enable →
  hw_reset → disable) → `irq_ignore=0`. Импульс reset при включённом IRQ
  не должен ловиться как касание.
- `SIFP_IOC_WAIT_IRQ` (13): `wait_for_completion(&done)` — блокирующее
  ожидание фронта. Именно поэтому HAL не поллит TA в покое.
- `SIFP_IOC_INIT` (16): poweron + request_irq + читает `spi-max-frequency`,
  `qcom,qup-id` → возвращает `dev_info` (speed, dev_id=qup_id, ta="sileadta",
  reserve=PKG_SIZE<<12).
- Полный список ioctl — в `silead_fp.h` (magic `'s'`, 10..30).
- Sensor IRQ — физическая линия от чипа (не от TA): чип сам дёргает gpio83
  при детекте пальца в NAV-режиме.

Рабочая auth/capture-последовательность (`native-lib.cpp`, `once`):

```
fp_driver_init:  SIFP_IOC_INIT → RESET(0) → CLR_IRQ → ENABLE_IRQ
keymaster64:      {0x205,2} → 152-байт blob
sileadta init:   0x59(8,0x1003) → 0x1f(2) → 0x20 SET_SPI_DEV=14
                 → 0x0e INIT(0x80064,0x493e00,0x64000) → chip id
                 → 0x59(0xf000,0x1004) cfg → 0x1b INIT2(9)
                 → 0x30 SET_KEY_DATA(blob) → 0x50 → 0x59(4,0x1013)
                 → 0x39 OTP → 0x0d NAV_SUPPORT
                 → 0x01(3) → 0x39 → 0x01(3) → 0x36(4,2) → 0x01(3)
                 → 0x37 → 0x01(3) → 0x01(3) → 0x60 → 0x56
arm+wait:        RESET → 0x01(3) → 0x0a NAV_START → 0x01(3) → 0x01(2)
                 → WAIT_IRQ (реальный фронт gpio83!)
dance:           0x01(3) → 0x01(0) → 0x38 → 0x01(3) → 0x01(1) → 0x38
loop ×8:         0x02 CAPTURE(0, retry?1:0) → 0x05 AUTH_STEP(0,0,r?1:0)
                 → 0x52 dump кадра (SEND_MODFD, до 4 fd)
end:             0x06 AUTH_END
```

Отличия от нашего `ta-load authirq`, которые могут быть критичны:

1. **WAIT_IRQ — настоящий edge на gpio83**, а не gpio-keys/NETLINK и не
   NAV-поллинг. У нас IRQ не сработал за 2 касания.
2. `RESET` делается **дважды**: в `fp_driver_init` и ещё раз прямо перед
   arm NAV.
3. `ENABLE_IRQ` держится всю сессию — фронт приходит ровно в момент
   касания, до mode dance.
4. `0x02` чередует `a1 = 0,1,1,1...` (retry-флаг со 2-й попытки), `0x05`
   получает `a2 = тот же флаг` — у нас, возможно, порядок аргументов
   чуть другой.
5. `0x52` идёт через `QSEECOM_IOCTL_SEND_MODFD_CMD_REQ` — до **4 пар
   (fd, offset)** в `qseecom_ion_fd_info`; у нас SEND_MOD умеет один буфер.

## 2. Gap-анализ: mainline vs Android

| Компонент | Android | Mainline сейчас | Что делать |
|---|---|---|---|
| TA load/send | libQSEEComAPI, до 4 shared fd | `qseecom_fp`: LOAD/SEND/SEND_MOD(1 fd) | Добавить SEND_MODFD (multi-fd) — нужен для `0x52`-дампа и, возможно, enroll/save |
| AP IRQ gpio83 | request_irq EDGE_RISING + wait ioctl | gpio-keys KEY_F13 (не сработал) | Свой misc-драйвер с vendor-semantics ioctl'ов |
| Power/reset | SIFP_IOC_INIT/RESET через драйвер | вручную в ta-load (gpio100/104) | Перенести в драйвер, ioctl-совместимо |
| TA storage | qsee_fts REE-fs listener в стоковом qseecom | не реализован | Для auth по инъецированным .dat не нужен; для enroll — нужен (фаза C) |
| Keymaster blob | keymaster64 {0x205,2} | уже реплицировано (`km_get_key_data`) | — |
| Шаблоны | securefs внутри TA + .dat кэш | инъекция 0x11+0x1c работает | — |
| libsl_fp_impl | vendor HAL | не нужен, если свой клиент | опционально: шим для прогона enroll на стоке |

## 3. План по фазам

### Фаза A — ядерный IRQ-драйвер (главный блокер)

Цель: `/dev/silead_fp` с минимальным vendor-совместимым набором ioctl.

1. В `kernel.patch` рядом с `qseecom_fp.c` добавить `drivers/misc/silead_fp.c`
   (или расширить существующий — отдельный файл чище):
   - DT уже есть (`/soc/silead_fp`): avdd gpio100, rst gpio104, irq gpio83,
     supplies pm6125_l9 (always-on уже добавлен), qup-id, spi-max-freq.
   - `probe`: regulator_get avdd/vddio + gpio request + pinctrl;
     `request_irq(gpio_to_irq(83), handler, IRQ_TYPE_EDGE_RISING)` →
     `disable_irq` сразу (как у вендора).
   - ioctl'ы: `INIT`(16, минимальная `fp_dev_init_t`), `RESET`(10),
     `ENABLE_IRQ`(11), `DISABLE_IRQ`(12), `WAIT_IRQ`(13, completion +
     timeout опционально), `IRQ_STATUS`(18), `CLR_IRQ`(14),
     `PWDN`(28 — power off/on, у вендора используется при ESD).
   - IRQ handler: `complete(done)` + (опционально) netlink/input event.
     `irq_ignore`-флаг на время RESET — как у вендора.
2. Отладка полярности/конфигурации линии (если EDGE_RISING молчит):
   - vendor `irq-gpios` flags = `0x2008` — в вендорских ядрах старшие биты
     кодируют режим (вероятно open-drain). Проверить варианты pinctrl:
     без pull / pull-up / pull-down; edge rising / falling / both;
     level low.
   - Быстрый тест без нового драйвера: `gpio-keys` уже показал, что
     evdev-путь не сработал — но можно проверить сырое состояние линии
     через `gpioget`/`gpioset` в sysfs или хотя бы `cat` gpio debug
     (осторожно: `/sys/kernel/debug/gpio` на нашем ядре безопасен —
     падал только на Android). Замерить idle-уровень gpio83 в покое и
     при касании.
3. `ta-load authirq` перевести с gpio-keys на `/dev/silead_fp` ioctl'ы
   (WAIT_IRQ вместо ожидания evdev).

### Фаза B — auth один-в-один с вендором

1. Портировать последовательность `once` из `native-lib.cpp` в `ta-load`
   (режим `authsnap`): порядок reset/arm/wait/dance/8×cap+step уже описан
   в §1; сверить аргументы `0x02`/`0x05` с impl-vtable (slot1/slot4).
2. Добавить в `qseecom_fp` ioctl `SEND_MODFD` — раскладка как у
   `QSEECOM_IOCTL_SEND_MODFD_CMD_REQ`: `qseecom_ion_fd_info e[4]` с
   (fd, cmd_buf_offset); для нас — до 4 TZ/dma-buf с offset'ами.
   Это понадобится и для `0x52` (frame dump), и, вероятно, для
   enroll/save-template команд с несколькими буферами.
3. Прогон: initramfs `fp/` уже содержит `.dat` + `silead.tpl` —
   `0x11` + `0x1c` ×2 + терминатор → `0x18` → auth.
   Критерий успеха: `CAPTURE=0` и `AUTH_STEP → 0` (вернёт fid).
   `0x52` после шага — контроль качества кадра.
4. Если снова `-1023`: сравнить экспортированный кадр с эталонным
   `snap.raw` (Android, `/mnt/128/silead-snap/`); снять метрики
   quality/cover через тот же приём, что в дизасме `fp_impl_auth_step`
   (пороги в конфиге TA @0x1a0/0x1a1). Возможные причины: кадр снят не в
   тот момент фронта, калибровка `0x36/0x37` не в том режиме, cfg из
   `silead.cfg` не загружен в TA (`0x59(0xf000,0x8004)` write-op).

### Фаза C — enroll (база шаблонов)

Варианты по возрастанию правильности:

1. **Быстрый**: на стоке пользователь заенролливает палец (Settings →
   отпечаток). `.dat` vendor удаляет после загрузки в TA, но их можно
   перехватить: временно убить `fingerprint-service`/`fingerprintext`
   сервисы до enroll или поставить backtrace-точку; либо вытащить
  .securefs/`.dat` из бэкапа. Полученные `.dat` → initramfs `fp/` →
   инъекция `0x1c` на mainline. sec_id берётся из `silead.tpl`.
2. **Правильный**: реализовать REE-fs listener в `qseecom_fp`
   (stock `drivers/misc/qseecom.c` из `ai-kernel_xiaomi_sm6225` —
   механика `qseecom_register_listener`/forward_tz_signal публична),
   чтобы TA могла хранить шаблоны в persistent fs. Тогда enroll через
   `ENROLL_START/STEP/END` (номера команд — из дизасма `libsl_fp_impl.so`
   или wire-трейса реального enroll на стоке через qsee-шим).
3. **Wire-трейс enroll на стоке** (нужен один раз, для протокола):
   вернуть Magisk-модуль `qseetrace` (шим `libQSEEComAPI.so`, исходник
   `tools/qseecom_shim.c`), пользователь проходит enroll в Settings —
   получим точную последовательность команд включая номера ENROLL_*.

### Фаза D — userspace-интеграция

- libfprint silead-драйвера нет → минимум: собственный демон/CLI
  (`ta-load enroll/verify` + простой протокол), позже fprintd-совместимый
  shim или порт драйвера в libfprint.
- Для десктопа: unlock по отпечатку — отдельная задача после verify.

## 4. Риски и открытые вопросы

- **`AUTH_STEP -1023`**: может быть не IRQ, а метрики качества/покрытия
  (TA сравнивает с порогами конфига до match). Фаза B.4 — диагностика.
- **Enroll persistence**: без REE-fs listener TA может не сохранять
  шаблоны между сессиями — тогда каждый бут нужна переинъекция `.dat`
  (приемлемо для PoC).
- **Две сессии TA**: на Android нельзя иметь две сессии `sileadta`
  одновременно (app_id один) — на mainline то же; сервисов-конкурентов
  нет, но следить за рестартами.
- **keymaster64 blob**: уже работает, но blob привязан к сессии —
  запрашивать на каждый init, не кэшировать.
- **`silead.cfg`**: если `0x59(0xf000,0x8004)` write-op вернёт ошибку —
   конфиг в TA уже зашит/загружен, write не обязателен (на Android
   вендор тоже получает -1015).

## 5. Артефакты (всё на месте)

| Что | Где |
|---|---|
| TA-образы sileadta | `out/fp-recon/sileadta.*`, `docs/fingerprint/stock/` (новое) |
| libsl_fp_impl.so, libQSEEComAPI.so | `out/fp-recon/android/` |
| Шаблоны .dat ×2, silead.tpl | `out/fp-recon/android/fpdata/`, initramfs `lib/firmware/fp/`, `docs/fingerprint/stock/` |
| Конфиг-дамп 19993 Б | `docs/fingerprint/stock/silead.cfg` |
| Вендорский драйвер (исходники) | `iDeadXD/ai-kernel_xiaomi_sm6225: drivers/input/fingerprint/silead/` |
| Рабочий frame-capture клиент | `/mnt/128/silead-snap/` (native-lib.cpp — эталон последовательности) |
| Наш клиент | `tools/ta-load.c` (режимы authirq/authnav/frameprobe/…) |
| Ядро: qseecom_fp + DT | `kernel.patch` (`drivers/misc/qseecom_fp.c`, `/soc/silead_fp`) |
| Wire-трейсы Android | `docs/fingerprint/silead-*.txt` |
