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
- `out/fp-recon/` (не в git): `sileadta.*`, `fpctzapp.*`, HAL-ы, `libsl_fp_impl.so`,
  `libQSEEComAPI.so`, `libion.so`, bionic-рантайм, `tz_a.bin`, `xbl_a.bin`,
  `dtbo_a.img`, `boot_a.img`.

## 7. Статус

`STATUS.md` и `docs/hardware-inventory.md`: «сканер отпечатка» — NOT SUPPORTED,
сенсор **Silead**, шина TEE-only. Предыдущая запись про FPC1020 исправлена.
