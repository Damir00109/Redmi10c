# Сканер отпечатка: **Silead**, не FPC — карта, доказательства, вердикт

Обновлено 2026-10-27 (вторая итерация). Продолжение
`docs/FINGERPRINT-HANDOFF.md`. **Важно: предыдущий вывод «FPC1020» для этого
экземпляра неверен** — живой сенсор Silead. Ниже — доказательства и что это
значит для mainline.

**Вердикт не изменился: на mainline без клиента к TA — NOT SUPPORTED.**
Шиной владеет secure world, но TA теперь другой: `sileadta`, а не `fpctzapp`.

---

## 0. TL;DR

| Что | Итог |
|---|---|
| Физический сенсор | **Silead** (chip id `0x6135a080`, читается стабильно) |
| HAL | `fingerprint.silead.default.so` → `libsl_fp_impl.so`, сервис выбирает класс `silead` |
| TA | `sileadta` (32-bit ARM QSEE TA), app id 6, грузится штатно |
| Транспорт | HAL → QSEECom → `sileadta` → `qsee_spi` (HAL шлёт `SET_SPI_DEV 14`) |
| FPC-стек | в образе есть, но **не используется**: `fpc_hal_open failed`, TA вернул `FPC_ERROR_NOT_SUPPORTED` |
| `ro.boot.fpsensor=fpc` | ставит ABL, **не соответствует** живому железу (cmdline boot-образа пуст — значение не из ROM) |
| Доступ из AP | закрыт: пины 14–17 — LPASS-остров, регистры SE5 вешают SoC |

---

## 1. Доказательства: сенсор Silead

Живой лог инициализации (полностью — `docs/fingerprint/silead-init-trace.txt`):

```
> 0x20 (SET_SPI_DEV) [14 0 0x0:0x0]      < 0 (SUCCESS)
> 0xe  (INIT) [0x80064:0x493e00:0x64000] < 1 (SUCCESS) [0x6135a080:0x6135a080:0x80000000]
[silead_impl] chipid=6135a080,6135a080,0 1 (1)
> 0x1b (INIT2) [9 0 ...]                 < 0 (SUCCESS) [0x200038:0xe30ff]
> 0x30 (SET_KEY_DATA)                    < 0 (SUCCESS)
> 0x50 (GET_VERSIONS)                    < 0 (SUCCESS) [0xf4638:0x1ff:0x0]
> 0x39 (GET_OTP)                         < 4 (SUCCESS) [0x789870a5:0x240000:0x0]
> 0xd  (NAV_SUPPORT)                     < 0 (SUCCESS) [0x2]
> 0xa  (NAV_START)                       < 0 (SUCCESS)
```

То есть TA реально разговаривает с чипом: стабильный chip id, OTP, калибровка,
nav. На FPC-чипе этот протокол не дал бы SUCCESS.

Дополнительно:

- `QSEECOM: qseecom_load_app: App with id 6 (sileadta) now loaded` — TA грузится.
- HAL-цепочка: `fingerprint.silead.default.so` (11 832 Б) → `libsl_fp_impl.so`
  (376 640 Б) → `/dev/silead_fp` (только питание/reset/IRQ) + TA по QSEECom.
- Версия silead HAL: `6.3.4(...XM-CAP-C3Q)` — совпадает с SKU устройства
  (`androidboot.product.hardware.sku=c3qn`).
- **Живое касание** (`docs/fingerprint/silead-touch-trace.txt`): IRQ приходит,
  `0x3 (NAV_CAPTURE_IMG)` → SUCCESS, `0xb (NAV_STEP)` → SUCCESS, HAL отдаёт
  `NAV_KEY_CLICK_DOWN/UP`. То есть TA реально снимает кадр с сенсора —
  команда захвата для будущего клиента: **`0x3`**.

## 2. Доказательства: FPC здесь ни при чём

- Сервис перебирает классы (`fortsense`, `silead`, `fpc`, `fpc_fod`, `goodix…`)
  и останавливается на `silead`.
- Когда очередь доходит до `fpc`, HAL грузится, но:
  ```
  E fpc_tac : fpc_tac_transfer TA returned error: 0x8cf1c28e (FPC_ERROR_NOT_SUPPORTED)
  E fpc_tac : sensor_command, Failed to send command: 0 to TA
  E fpc_fingerprint_hal: fpc_hal_open failed
  ```
  `fpc_tee_sensor_init()` получает от TA `NOT_SUPPORTED` — FPC-чипа на шине нет.
  (Оговорка: silead-драйвер держит reset gpio104 и IRQ gpio83, так что
  FPC-сенсор мог быть и не сброшен; но chip id и полный SUCCESS-протокол
  silead — независимое и более сильное доказательство.)
- `ro.boot.fpsensor=fpc` приходит от ABL: cmdline boot-образа пуст (проверено
  разбором `boot_a.img`), значит значение не из ROM. На этом экземпляре оно
  не отражает железо (вероятно, дефолт платформы).

## 3. Шина и почему AP не может

- `silead_fp` в DT: `qcom,qup-id = <6>`, `spi-max-frequency = 0x493e00`
  (4.8 МГц), пины: avdd gpio100, reset gpio104, irq gpio83.
- HAL перед INIT шлёт `SET_SPI_DEV 14` — TA открывает SPI через `qsee_spi`
  (secure-сервис QTEE).
- Пины **gpio14–17 — LPASS-остров**: `pinctrl-khaje.c` резервирует их навсегда
  (любой AP-доступ к TLMM этих линий роняет SoC; проверено ранее свипом).
- AP-side `geni-spi` на SE5 (`spi@4a94000`) вешает SoC на probe — проверено
  2026-10-27 (см. §5).

Итог: как и для FPC, данные идут только через TA. Свой SPI-драйвер на AP
невозможен.

## 4. Что это меняет в плане TZ

Цель клиента — **`sileadta`**, а не `fpctzapp`:

- TA проще: 32-bit ARM, 1.2 МБ кода, протокол виден в трейсе
  (`docs/fingerprint/silead-init-trace.txt`): команды `0x0e INIT`,
  `0x1b INIT2`, `0x30 SET_KEY_DATA`, `0x50 GET_VERSIONS`, `0x39 GET_OTP`,
  `0x36/0x37 CALIBRATE`, `0x0d NAV_SUPPORT`, `0x0a NAV_START`,
  `0x59 SEND_CMD_WITH_BUF_GET` (буферные команды), `0x20 SET_SPI_DEV`.
- Проблема та же: в mainline `qseecom` нет `APP_LOAD`, а `app_get_id` для
  незагруженного TA даёт `-ENOENT` (проверено на `fpctzapp`; для `sileadta`
  будет то же).
- Дальше — тот же путь: APP_LOAD → UAPI → либо свой клиент, либо шим
  `libQSEEComAPI` + оригинальный `libsl_fp_impl.so`.

Плюс: silead-протокол известен (в отличие от FPC), и в открытом виде есть
libfprint-драйвер для некоторых Silead — но он бесполезен без доступа к шине.

## 5. Живые тесты 2026-10-27 (AP-side)

1. `spi5` (SE5 `4a94000`) с pinctrl `qup5` → `khaje-pinctrl: error -EINVAL:
   pin-14 (4a94000.spi)` (пины зарезервированы), probe не дошёл до регистров.
2. `spi5` без pinctrl → **зависание SoC** на probe (экран с подсветкой, USB не
   поднимается). Регистры SE5 для non-secure закрыты.
3. `qcom_scm_qseecom_app_get_id("fpctzapp")` → `-ENOENT` (TA не предзагружен,
   APP_LOAD в mainline нет).
4. Unbind silead-драйвера на Android → **ребут SoC**. Не повторять.

## 6. Артефакты

- `docs/fingerprint/silead-init-trace.txt` — полный трейс инициализации
  (команды TA с аргументами и статусами).
- `docs/fingerprint/silead-touch-trace.txt` — трейс касания: IRQ →
  `NAV_CAPTURE_IMG (0x3)` → `NAV_STEP (0xb)` → key-события.
- `out/fp-recon/` (не в git): `sileadta.*`, `fpctzapp.*`, HAL-ы, `libsl_fp_impl.so`,
  `libQSEEComAPI.so`, `libion.so`, bionic-рантайм, `tz_a.bin`, `xbl_a.bin`,
  `dtbo_a.img`, `boot_a.img`.

## 7. Статус

`STATUS.md` и `docs/hardware-inventory.md`: «сканер отпечатка» — NOT SUPPORTED,
сенсор **Silead**, шина TEE-only. Предыдущая запись про FPC1020 исправлена.

---

## 8. Phase 0/1 (2026-10-27): TA загружается из mainline, команды ходят

Это главный результат: **ветка 4B работает**. Что сделано:

1. `qcom_scm_qseecom_app_load()` в `drivers/firmware/qcom/qcom_scm.c` —
   `APP_START` (owner QSEE_OS, svc APP_MGR, cmd 1) с аргументами
   `(mdt_len=0, img_len, phys)`. TEE возвращает `resp_type=0xEE01` и app id.
2. `drivers/misc/qseecom_fp.c` — misc-узел `/dev/qseecom-fp` с двумя ioctl:
   - `LOAD` (`_IOWR(0xf5,1,...)`): образ TA из userspace → TZ-память → APP_START,
     наружу отдаётся `app_id`;
   - `SEND` (`_IOWR(0xf5,2,...)`): request/response в TA.
3. `tools/ta-load.c` — собирает образ из `.mdt` + `.b00..b07` (сегменты по
   `p_offset`, как libQSEEComAPI) и грузит его; режим `probe` шлёт цепочку
   команд silead.

Живой лог (rescue-образ, `fastboot boot`):

```
qseecom: APP_START img_len=1644558 phys=0xfdb00000 status=0 result=0x0 resp_type=0xee01 data=0x3
qseecom_fp: loaded sileadta, app_id 3
cmd 0x50(0,0,0,0) -> 0x000f4638 0x000001ff 0x00000000 ...   # GET_VERSIONS
```

`GET_VERSIONS` вернул **ровно те же** `0xf4638 / 0x1ff`, что silead-HAL на
Android (`ta version:v511, algo version:v1001016`) — значит протокол и
транспорт совпадают.

Что пока не работает: `SET_SPI_DEV(0x20, 14)` → `INIT(0x0e)` даёт
`[0, 0, 0x80000000]` без chip id (на Android — `0x6135a080`). Сенсор сброшен
(gpio104), питание включено (gpio100 + L9 1.8 В), перебор SPI dev id 0..31
даёт один и тот же ответ.

**Причина найдена (2026-10-28):** на Android `SET_SPI_DEV` идёт через
**`silfp_ca_send_modified_command`** — modified-команду с shared-буфером
(ion), а `GET_VERSIONS` — через normal-команду. Наш `SEND` умел только
normal, поэтому SPI-устройство в TA не открывалось, и `INIT` читал нули.

**Сделано:** ioctl `SEND_MOD` (`_IOWR(0xf5,3,...)`) — раскладка как у
вендорского HAL: `req[0]=cmd`, `req[4]=phys(shared)` (подставляет ядро),
`req[8]=size`, `req[12]=arg`, ответ по смещению **`+0x500` в том же буфере**
(поэтому в запросе передаётся `rsp_offset`). После этого
`SET_SPI_DEV(0x20, 6|14)` возвращает **`[0,0]` (SUCCESS)** — как на Android.

**Что осталось:** `INIT(0x0e)` возвращает `-1007` — по таблице кодов HAL это
**CONFIG_INVALID**, и в самом TA это подтверждается строкой
`null or invalid cfg` (найдена в `sileadta.b02`: функция записи таблицы
регистров проверяет указатель конфига и длину). То есть SPI-устройство в TEE
открывается, но у TA **нет конфига**, и он отказывается инициализировать чип.

Проверено и исключено: перебор SPI dev id 0..31 (через правильный modified-
транспорт), power-cycle (gpio100 off/on), reset-импульс (gpio104 low 5 мс →
high), питание L9 1.8 В, воспроизведение полной последовательности Android
(`0x59` → `0x1f` → `0x20` → `0x0e`). Файл
`/data/vendor/silead/config/6135a080-6135a080-00000000.cfg` — это **дамп**,
который HAL сохраняет при инициализации (`save ...cfg` в логе), а не входной
конфиг; `fpconfig.dat` на Android тоже отсутствует, но инициализация проходит.

**Решение (2026-10-28, ночь):** шим `libQSEEComAPI.so` через Magisk
(`/data/adb/modules/qseetrace`) с логом в `/data/local/tmp/qsee-trace.log`
показал реальную раскладку запроса:

```
normal:   [0]=cmd, аргументы в [16],[20],[24],[28]
modified: [0]=cmd, [4]=phys(sb), аргументы в [12],[16],[20],[24]
```

Наш `ta-load` клал аргументы в `[4]` — TA получал мусор и отвечал
`CONFIG_INVALID`. После исправления **всё работает**:

```
0x0e INIT   -> 0x6135a080 0x6135a080 0x80000000   (chip id!)
0x39 GET_OTP-> 0x789870a5 0x00240000 0x00000000   (OTP, как на Android)
0x1b INIT2  -> 0x00200038 0x000e30ff
0x59(0xf000,0x1004) -> 0x4e19 + 0xf000 байт конфига
0x0d NAV_SUPPORT -> 2
```

То есть mainline через TA **читает chip id и OTP с реального сенсора**.
NAV_START проходит, `NAV_STEP` отвечает; захват кадра требует касания
(без пальца TA отдаёт пустой ответ) — это следующий шаг, когда телефон
будет под рукой.

**Касание подтверждено (mainline, 2026-10-28):** в NAV-режиме TA отдаёт
события `0x8`/`0x9` (NAV_KEY_CLICK_DOWN/UP) при прикосновении, в покое —
`0x3e8` (нет события). То есть сенсор полностью живой через TEE.

Карта команд (скан id 0..0x60 после INIT): `0x51` GET_CHIPID,
`0x54`/`0x55` ok, `0x56` TEST_GET_IMG_INFO → `0x400040` (64×64),
`0x60` CHECK_BROKEN ok; `0x52` TEST_IMAGE_CAPTURE и `0x23`
CAPTURE_IMG_RAW отвечают `SHARED_MAP_FAILED` (-1012) — TA не может
замапить наш TZ-буфер. Нужен вендорный путь shared-буфера (ion + трансляция
адресов в ядре) либо трейс Android-enroll (требует PIN).

Инструмент трассировки сохранён: `tools/qseecom_shim.c` — шим
`libQSEEComAPI.so` (Magisk-модуль кладёт его в `/vendor/lib64`, рядом
`libQSEEComR.so` — оригинал с пропатченным SONAME; лог в
`/data/local/tmp/qsee-trace.log`).

Проверено и исключено: перебор SPI dev id 0..31, power-cycle (gpio100
off/on), reset-импульс (gpio104 low 5 мс → high), питание L9 1.8 В,
воспроизведение полной последовательности Android (`0x59` → `0x1f` →
`0x20` → `0x0e`).

**Новая гипотеза (2026-10-28):** при загрузке через `fastboot boot` ABL
может не выполнять ту же инициализацию пинов/QUP, что при обычной
загрузке — тогда как Android (нормальный boot) получает уже
сконфигурированную шину. Проверить это можно только нормальной
загрузкой mainline (не `fastboot boot`), что противоречит правилу
«эксперименты — только `fastboot boot`». Отдельно: island-пины 14–17 в
LPASS-пинконтроллере `UNCLAIMED`, а основной TLMM инспектировать нельзя —
`/sys/kernel/debug/gpio` на Android роняет SoC (`msm_gpio_dbg_show`,
synchronous external abort).

Артефакты: `/dev/qseecom-fp`, `ta-load`, `lib/firmware/qcom/sileadta.*`
(локально, не в git).

## Обновление: IRQ ожидается перед auth capture

Android `silfp_impl_init()` после `SIFP_IOC_INIT` вызывает
`SIFP_IOC_RESET(0)`: reset GPIO104 low 5 мс → high, при этом драйвер
временно включает rising-edge IRQ GPIO83. Mainline теперь регистрирует GPIO83
как `gpio-keys` IRQ (`KEY_F13`, оба фронта; TLMM input с pull-down).

Без физического касания `CHECK_ESD` после полного init возвращает 4; после
`MODE_DOWNLOAD [3,0]` TA отвечает `INT_INVALID` (-1036), а следующий
`CHECK_ESD` — `DETECTED_ESD` (-1035). Поэтому старые прогоны без пальца не
доказывали аппаратную ESD-защёлку. Режим `ta-load ... authwait` ждёт реальный
IRQ GPIO83 (до 5 минут) перед отправкой Android auth-команд.

`SET_KEY_DATA(0x30)` решён (2026-10-30): QSEECom-шим на Android показал, что
HAL сначала обращается к резидентному TA `keymaster64` командой `0x205`
(arg=2); ответ — `[status][offset=0x0c][size=0x98]` + 152-байтный session
blob (префикс стабилен, хвост меняется между запусками — хардкодить нельзя).
`ta-load` теперь реплицирует это в `km_get_key_data()`: APP_LOOKUP по имени
`keymaster64` (образ не нужен, TA резидентный, app_id=1), plain-команда
`{0x205,2}`, извлечение blob из ответа и подача в shared buffer перед
`0x30`. Результат на mainline: `keymaster64 app_id 1`, `rsp: status=0 off=12
size=152`, `SET_KEY_DATA -> 0 (SUCCESS)` — было -1011/-1013 с нулевым
буфером.

В rescue GPIO83 запрошен через `gpio-keys`, код `KEY_F13`;
`/proc/interrupts` показывает зарегистрированный `msmgpio 83 Edge
fingerprint_irq`. `authwait` — AP IRQ-тест, но линия не сработала при двух
попытках касания.

Позже NAV-поллинг (`NAV_CAPTURE_IMG` + `NAV_STEP`) вернул `0x8`/`0x9`,
подтвердив, что TA видит touch через secure SPI без AP IRQ. Первый `authnav`
поймал `0x8` сразу после запуска, но все восемь `CAPTURE_IMG` вернули
`MOVE_TOO_FAST` (-1018); это мог быть стартовый reset edge. Новый `authnav`
сбрасывает начальные события и ждёт свежего NAV-события.

2026-10-30: `authnav` теперь использует полный init с реальным key-data
из keymaster64 — `SET_KEY_DATA` возвращает SUCCESS. Осталось дождаться
свежего касания с этим ключом для проверки `CAPTURE_IMG`/`AUTH_STEP`.

Отдельно найден эксплуатационный нюанс fastboot: если `fastboot boot`
«зависает» или образ возвращает телефон в fastboot мгновенно после
`Booting OKAY`, это признак подвешенного состояния, а не битого образа.
Лечение: `fastboot reboot bootloader` → `fastboot set_active b` →
`fastboot boot` (см. AGENTS.md).

## Обновление 2026-10-30 (вечер): протокол БД решён, шаблоны грузятся

Дизассемблинг `libsl_fp_impl.so` (`silfp_impl_load_user_db`,
`silfp_storage_get_sec_id`) + `silead.tpl` дали точный протокол загрузки
базы шаблонов:

- `0x11 LOAD_USER_DB` — **с data-буфером = путь к tpl-каталогу**, `a0` =
  длина буфера (включая NUL). Реверс `silfp_impl_load_user_db` →
  `cmd_load_user_db(path, &out)`: CA кладёт path-строку в shared-буфер и
  передаёт её размер в a0. Подтверждено на железе (dbpath, /tmp/db2.log):
  `0x11(26)` с `/data/vendor_de/0/fpdata` в буфере → **status 0**;
  bare `0x11(0)` → -1012 (TA видит пустой буфер).
- `0x1c LOAD_TEMPLATE` — буфер = `[u64 sec_id][сырое .dat]`, `a0=размер
  буфера`, fid в аргументах **не** передаётся. `sec_id` читается из
  `silead.tpl` (файл = заголовок 20 байт + 10 записей по 96 байт; fid@+0,
  имя файла@+8, sec_id@+0x50). У обоих пальцев sec_id один —
  `0x9857ae944659ebde` (per-user db id).
- Терминатор — `0x1c` с буфером `[0xff 0xff]` (2 байта).
- Вендор после успешной заливки шаблона **удаляет .dat** (`storage_remove`)
  — миграционный путь: шаблоны живут внутри TA/securefs, .dat — только
  кэш для перезаливки.

Результат на mainline: `0x1c(306552)` и `0x1c(96232)` приняты (rsp=4, без
`SHARED_MAP_FAILED`), терминатор принят. `0x11` с длиной пути в a0
принимается (rsp[4]=0) — сессия загрузки открывается; bare-вариант (a0=0)
отвергается -1012. `0x13 GET_DB_COUNT` стабильно отвечает одними и теми же
словами, что `0x50 GET_VERSIONS` — похоже на stale rsp-буфер, счётчик БД
через `0x13` так и не подтверждён (в вендорских трейсах `0x11`/`0x13`/`0x1c`
вообще отсутствуют: на Android база уже лежит в securefs TA).

`0x59` опкоды тоже выверены по wire-дампам `qsee-trace.log`:
- раскладка req: cmd@0, a0@+0x0c, a1@+0x10, phys(u64) ядро пишет в +4;
- `0x59(8, 0x1003)` init → rsp 8;
- `0x59(0xf000, 0x1004)` чтение конфига — нужен буфер **61440 байт**
  (с 4КБ был -1012 SHARED_MAP_FAILED); ответ `0x4e19`=19993 байта —
  ровно размер `silead.cfg`, т.е. это выгрузка конфига/калибровки из TA;
- `0x59(0xf000, 0x8004)` (запись cfg) вендору тоже отвечает -1015 —
  конфиг в TA уже есть, запись не нужна;
- `0x59(4, 0x1013)` → rsp 4 (+sub-status -1007 у нас и у вендора);
- `0x36(4,2)` вендору тоже -1013 — не блокер для auth.

Вендорский auth-цикл (silead-auth-trace.txt): палец держится непрерывно
от NAV-события через весь танец режимов — `NAV_STEP=8` → `MODE[3]→MODE[0]`
(без NAV_END!) → `0x38` → `MODE[3]→MODE[0]` → `0x38` → `MODE[3]→MODE[1]`
→ `CAPTURE[0,0,0,0]` → `AUTH_STEP` → `-1031`/`0`. При mismatch — ретрай с
флагами `CAPTURE[0,1]`/`AUTH_STEP[0,0,1]`, при двух mismatch — полный
`MODE[3]→MODE[0]` сброс и новый заход. Успех: `AUTH_STEP → 0` возвращает
fid (`0xff0cad64`), затем `GET_AUTH_OBJ(69,2)` → `0x59(4,0x1011)` →
`AUTH_END` → ре-арм NAV (`MODE[3],MODE[0],0x38,MODE[3],0x60,0x0a,
MODE[3],MODE[2]`).

`NAV_STEP`: 8 = finger down, 9 = finger up — события встают в очередь на
ФРОНТ касания, очередь переживает ре-арм (нужен drain при старте).
Детектор присутствия пальца — не NAV_STEP, а пара `CHECK_ESD=4` +
`NAV_CAPTURE=0` (в простое -1035/-1026).

Осталось: `AUTH_STEP` стабильно `-1023 QUALITY_COVERAREA_FAILED` при
успешных кадрах (esd=4, cap=0). В вендорском коде -1021..-1024 — класс
mistouch: обработчик не ретраит кадр, а ждёт СВЕЖИЙ фронт касания
(lift + re-press) и повторяет цикл. В `authnav` добавлен этот путь:
на -1023 — `MODE[3]→MODE[0]`, ждать отпускания, ждать нового нажатия,
`MODE[3]→MODE[1]`, capture заново.

## Impl-vtable (libsl_fp_impl.so .data.rel.ro @0x5d5c0, декодировано)

`silfp_cmd_*` — трамплины через vtable; реальные номера команд:

- slot0 `wait_finger_status` → `0x01` MODE_DOWNLOAD, arg = `mode | (charging << 8)`
  (bfi w9, w1, #8, #8 — состояние зарядки в битах 8-15; телефон на USB → 0x101)
- slot1 `capture_image` → `0x02(a,b)` прямо
- slot2 `nav_capture` → `0x03`
- slot3 `auth_start` → `0x04(0,0,0,0)`
- slot4 `auth_step` → `0x05(a>>32, a_lo, b, c)` — u64-параметр раскладывается
  на arg0/arg1; retry-индекс — arg2 (совпадает с нашим send_cmd(0x05,0,0,retry,0))
- slot5 `auth_end` → `0x06`
- slot10 → `0x0d`, slot15 → `0x0f`+ca_close, slot36 → `0x38` (CHECK_BROKEN),
  slot38 → `0x39`, slot40 → mod `0x59`, slot42 → mod `0x36`, остальные —
  0x09/0x0a/0x0b/0x11/0x12/0x13/0x14/0x15/0x16/0x19/0x1a/0x1d/0x21/0x22/0x23/
  0x24/0x25/0x26/0x27/0x28/0x37/0x50/0x51/0x53/0x54/0x55.

`impl_wait_finger_down` (0x3ae68): `dev_wait_finger_status` (блок на
kernel IRQ через silead_fp-драйвер) → `cust_finger_down_pre_action` →
`cmd_download_normal` (= **hw_reset: rst-GPIO 5ms low/3ms high** + MODE[3])
→ `dev_clear_irq` (драйвер чистит sensor INT по SPI) →
`cmd_wait_finger_status(1, charging)` = MODE[0x101] → check_esd-цикл
(на -1035 ESD → `dev_pwdn(2)` = avdd off/on 200ms + слип).

Дельта vs наш путь: у вендора rst-тоггл при каждом finger-down и charging
в аргументе MODE. В tl30 добавлены оба. `dev_clear_irq` (SPI-запись в
сенсор) недоступен напрямую — SPI принадлежит TZ; возможно покрывается
командой `0x0d`.

## Коды ошибок Silead (enum из `libsl_fp_impl.so`, 2026-11-02)

Таблица имён в impl идёт подряд от `BAD_PARAMS` = -1000. Ключевые:

| код | имя | | код | имя |
|---|---|---|---|---|
| -1000 | BAD_PARAMS | | -1023 | QUALITY_COVERAREA_FAILED |
| -1007 | CONFIG_INVALID | | -1024 | FAKE_FINGER |
| -1012 | SHARED_MAP_FAILED | | -1025 | GAIN_IMPROVE_TIMEOUT |
| -1013 | TA_OP_FAILED | | **-1026** | **SPI_TIMEOUT** |
| -1015 | REQ_CMD_UNSUPPORT | | -1028 | SPI_TRANSFER_FAILED |
| -1016 | STO_OP_FAILED | | -1031 | AUTH_MISMATCH |
| -1018 | MOVE_TOO_FAST | | -1032 | TEMPLATE_INVALID |
| -1019 | DETECT_NO_FINGER | | **-1035** | **DETECTED_ESD** |
| -1021 | QUALITY_FAILED | | -1036 | INT_INVALID |
| -1022 | COVERAREA_FAILED | | -1069 | DETECTED_MIS_TOUCH |

Поправка к прежним выводам: `-1026` — это **SPI_TIMEOUT**, а не «нет пальца»,
а `-1035` — **DETECTED_ESD**. На mainline idle-опрос стабильно даёт
`esd=-1035 cap=-1026`, у вендора в том же состоянии `esd=4 cap=0` →
транспорт/питание сенсора на mainline нестабильны, что согласуется с
`-1023` на «чистых» кадрах.

Имена команд (тот же порядок в impl): … SET_GID(0x10), LOAD_USER_DB(0x11),
FP_REMOVE(0x12), GET_DB_COUNT(0x13), GET_FINGERPRINTS(0x14), …,
LOAD_AUTH_ID(0x18), GET_AUTH_OBJ(0x19), UPDATE_CFG(0x1a), INIT2(0x1b),
LOAD_TEMPLATE(0x1c), SAVE_TEMPLATE, UPDATE_TEMPLATE, SET_LOG_MODE(0x1f),
SET_SPI_DEV(0x20), SET_NAV_MODE, CAPTURE_IMG_PRE/RAW/AFTER, CHECK_ESD(0x38),
GET_FINGER_STATUS, GET_OTP(0x39), …, TEST_IMAGE_CAPTURE, TEST_GET_IMG_INFO,
TEST_DUMP_DATA, TEST_SPEED, CHECK_BROKEN(0x60). Есть ENROLL_START/STEP/END.

Питание: сток — `silead_fp { avdd-supply, vddio-supply = pm6125_l9;
regulator-always-on }`. У нас `pm6125_l9a` без always-on, с
`regulator-allow-set-load` и без потребителя-сканера → RPM может держать
LDO в LPM. TA пишет файлы через `qsee_fts_*` (REE FS-листенер), но в SFS
(`/mnt/vendor/persist/data`) записей silead нет; HAL сам
`QSEECom_register_listener` не вызывает.

## Ночь 2026-11-02/03: idle-эталон, питание, IRQ-детект

**Главная находка:** на Android HAL в покое **не опрашивает TA**. После
`NAV_START` он уходит в `silfp_msg_wait_finger_status` и ждёт netlink-сообщение
от ядерного драйвера (`silfp_netlink_send(SIFP_NETLINK_IRQ)` из обработчика
gpio83). Только по IRQ идут `MODE[3]`/`MODE[0]`/`CHECK_ESD`.

Отсюда: наш прежний способ — поллинг `0x03`/`0x0b` — не вендорский. В покое он
даёт `esd=-1035` и `cap=-1026` (SPI_TIMEOUT, ~506 мс на вызов). Это НЕ признак
поломки: сенсор в этот момент в sleep-detect и на NAV-захват не отвечает.
Поэтому idle-статистика не годится как критерий; сравнение валидно только
в момент касания.

**`dev_clear_irq`** в вендорском драйвере (`silead_fp.c`, `SIFP_IOC_CLR_IRQ`) —
это всего лишь `reinit_completion(&fp_dev->done)`, никакого обращения к железу.
Значит его недоступность на mainline ни на что не влияет.

**Питание:** стоковый DT: `silead_fp { avdd-supply, vddio-supply = pm6125_l9;
regulator-always-on }`. У нас было без always-on (`requested_microamps=0`).
Добавили `regulator-always-on` + `regulator-initial-mode = HPM` (1.8 В,
`pm6125_l9a`). Idle-поведение не изменилось (как и ожидалось после находки
про IRQ), но конфигурация теперь соответствует стоку — оставлено.

**Коды ошибок** (полный enum из impl, см. таблицу выше) уточнили трактовку:
`-1026` = SPI_TIMEOUT, `-1035` = DETECTED_ESD, `-1023` = QUALITY_COVERAREA_FAILED.

**Новый режим `ta-load ... authirq`** (вендорский путь целиком):
1. init + `0x59(0xf000,0x1004)` + `0x1b` + `0x30(keymaster)` + `0x50` +
   `0x39` + `0x0d` + `0x36/0x37` + `0x60`;
2. `0x10` SET_GID → `0x11(path,len)` → `0x1c` на каждый `.dat` → терминатор
   `0xffff` → `0x18` (печатает auth-id);
3. `0x04` AUTH_START → NAV setup (`0x38`,`0x01(3)`,`0x60`,`0x0a`,`0x01(3)`,`0x01(2)`);
4. **ждёт аппаратный IRQ** gpio83 (не поллинг);
5. `MODE[3]→MODE[0]` ×2 с `CHECK_ESD` → `MODE[3]→MODE[1]` → `0x02` CAPTURE →
   `0x05` AUTH_STEP;
6. при `0` — `0x19(69,2)` GET_AUTH_OBJ и `0x06` AUTH_END, печатает `AUTH_OK fid`;
7. иначе mistouch-цикл с перевооружением.

Шаблоны и `silead.tpl` теперь кладутся в initramfs (`/lib/firmware/fp/`)
скриптом `tools/rebuild-rescue.sh` — серийная заливка 400 КБ больше не нужна.

Проверено автономно: `0x11` → 0, оба `0x1c` → 0, `0x18` → живой auth-id,
тест переходит в ожидание IRQ.
