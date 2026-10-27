# Отпечаток пальца (FPC1020): карта, доказательства, вердикт

Обновлено 2026-10-27. Продолжение `docs/FINGERPRINT-HANDOFF.md` — здесь
закрыт feasibility gate (вопросы A и B) и зафиксирован результат живой
проверки на устройстве.

**Вердикт: на mainline без клиента к TA — NOT SUPPORTED.**
Шина SPI сенсора принадлежит secure world (QTEE), а не AP; прямая попытка
поднять `geni-spi` на SE5 из non-secure мира **вешает SoC**. Единственный
теоретический путь — клиент к TA `fpctzapp` через QSEECom (ветка 4B), это
большой реверс-проект и в текущем объёме не делается.

---

## 1. Ответ на feasibility gate

**Вопрос A: какой QUP/CS использует TA?**
QUPv3_0 **SE5**, `spi@4a94000`, chip-select 0, `spi-max-frequency` 4.8 МГц.
Доказано двумя независимыми способами (§3).

**Вопрос B: доступен ли этот QUP из AP?**
**Нет.** Пины gpio14–17 — island-owned (LPASS), AP не имеет права их муксить
(наш же `pinctrl-khaje.c` их резервирует: доступ к TLMM сбрасывает SoC).
Регистры SE5 для non-secure тоже закрыты: probe `geni-spi` на `4a94000`
намертво вешает SoC (§5, тест 2).

Итог: ветка 4A (свой SPI-драйвер на AP) закрыта аппаратно. Остаётся 4B.

---

## 2. Что за железо (подтверждено)

| Факт | Откуда |
|---|---|
| Чип **FPC1020** (варианты A1–A5 в строках TA) | `strings` по `fpctzapp.b02` |
| `ro.boot.fpsensor=fpc` | `getprop` |
| HAL — AIDL `android.hardware.biometrics.fingerprint` v4 | `/vendor/etc/vintf/manifest/android.hardware.biometrics.fingerprint-service.xiaomi.xml` |
| HAL-бинарь `/vendor/lib64/hw/fingerprint.fpc.default.so` (174 984 Б) | устройство |
| HAL линкует `libQSEEComAPI.so` + `libion.so`, зовёт `QSEECom_start_app` с именем **`fpctzapp`** | `readelf -d` + строки HAL |
| HAL ↔ ядро только за электрику: sysfs `fpc1020/{power,hw_reset,irq,regulator_enable,...}` | строки HAL (`fpc_sysfs_node_write`), vendor-драйвер |
| Вендорский драйвер `fpc1020_platform_tee.c` SPI **не трогает** (868 строк, «spi» только про рельс `vcc_spi`) | `drivers/input/fingerprint/fpc/fpc1020_platform_tee.c` |
| DT (сток): `fpc1020` — IRQ tlmm 83, reset tlmm 104, `fpc,enable-on-boot` | live DT + `khaje-idp.dtsi:238` |
| Соседний вариант того же места: `silead_fp` с `qcom,qup-id = <0x06>`, `spi-max-frequency = <0x493e00>` | `khaje-idp.dtsi:272` |

Сенсор — обычный SPI-slave: HWID читается как «записать адрес `0xfc`, прочитать
2 байта», старший байт `0x10` (FPC1020). Это из открытого референсного драйвера
FPC (`fingerprint-cards/capacitive_device_driver`,
`mt6797/platform_driver/fpc1022_tee.c: FPC102X_REG_HWID 252`). Референс также
даёт тайминги reset: high 100 µs → low 5 мс → high 5 мс.

---

## 3. Доказательство A: TA ходит в SPI через secure-сервис, dev_id 5

### 3.1 Импорты TA

`fpctzapp` — QSEE TA (aarch64 ELF, сегменты `.mdt` + `.b00…b07`).
В `.dynamic`/`.rela.plt` (сегмент `.b07`) видны импорты secure-мира:

```
qsee_spi_open, qsee_spi_write, qsee_spi_close, qsee_spi_full_duplex,
qsee_register_shared_buffer, qsee_prepare_shared_buf_for_secure_read,
qsee_fts_read_file/write_file/encrypt/decrypt, qsee_hmac, ...
```

То есть SPI выполняется **на стороне TEE** (сервис `qsee_spi`), а не через
ядерный драйвер.

### 3.2 Дизасм: какой dev_id

Код TA — сегмент `.b02` с vaddr 0. PLT-заглушки в начале:

| stub | GOT | импорт |
|---|---|---|
| `0x10` | `0x39b020` | `qsee_spi_write` |
| `0x20` | `0x39b028` | `qsee_spi_open` |
| `0x30` | `0x39b030` | `qsee_spi_close` |
| `0x40` | `0x39b038` | `qsee_spi_full_duplex` |

```
fpc_spi_init   (0x5788): mov w0, #5 ; b 0x20      → qsee_spi_open(5)
fpc_spi_destroy(0x5790): mov w0, #5 ; b 0x30      → qsee_spi_close(5)
fpc_pal_spi_write    (0x5734): ... bl 0x10        → qsee_spi_write(5, ...)
fpc_pal_spi_writeread(0x5798): ... bl 0x40        → qsee_spi_full_duplex(5, ...)
```

### 3.3 Таблица QUP в образе TEE

`tz_a` (4 194 304 Б, aarch64 ELF) содержит таблицу SE-баз по адресу файла
`0x11f400`, записи по 32 байта `{idx, base, idx, ptr}`:

```
idx 0 → 0x04a80000   (SE0)
idx 1 → 0x04a84000   (SE1)
idx 2 → 0x04a88000   (SE2)
idx 3 → 0x04a8c000   (SE3)
idx 4 → 0x04a90000   (SE4)
idx 5 → 0x04a94000   (SE5)   ← qsee_spi_open(5)
idx 6 → 0x0ab80000   (LPASS QUPv3, дальше 0xab84000, …)
```

Рядом строки `gcc_qupv3_wrap0_s0_clk … s5_clk`, `num_spi_cores`,
`is_qupv3_ac_stubbed` — это конфиг SPI-ядра TEE (QUP access control).

**Итого A: SE5 = `spi@4a94000`, CS0.** Косвенное подтверждение: у `silead_fp`
в стоковом DT `qcom,qup-id = <0x06>` — та же нога (тот же footprint, те же
IRQ 83 / reset 104); нумерация Silead 1-based даёт тот же SE5.

---

## 4. Доказательство B: шина закрыта для AP

### 4.1 Пины — LPASS-остров

`drivers/pinctrl/qcom/pinctrl-khaje.c` (наш mainline-драйвер) резервирует
gpio14–17 навсегда:

> gpio14/15/16/17 (WEST tile) kill the SoC instantly on register access —
> proven by the Android sysfs export sweep (marker stopped at each of them,
> instant reboot). Keep them reserved FOREVER: on this SoC they are LPI pins
> owned by the LPASS audio island (stock DT: `lpi_i2s3_*` under
> `lpi_pinctrl@ac40000`, `qupv3_se5_spi`); Qualcomm's own downstream khaje
> driver reserves them too.

То есть даже mux пинов из AP невозможен — любой доступ к TLMM-регистрам этих
линий роняет SoC. Настраивать их может только остров/secure-мир.

### 4.2 Регистры SE5

XBL-образ содержит карту устройств, где весь блок QUPv3_0 помечен как
non-secure:

```
0x04A00000, 0x000D0000, "QUPV3_0_GSI", AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE
```

но фактически SE5 защищён access control'ом (см. 4.3): TEE держит его как
свой SPI-контроллер, и non-secure обращение к регистрам вешает фабрику.

### 4.3 Живые тесты (2026-10-27, rescue-образ через `fastboot boot`)

В `sm6225.dtsi` добавлен `spi5: spi@4a94000` (geni-spi, FIFO, GIC_SPI 332),
в board-DTS — `&spi5 { status = "okay"; spidev@0 … }`.

**Тест 1 — с pinctrl `qup5`:**

```
[2.904880] khaje-pinctrl 400000.pinctrl: error -EINVAL: pin-14 (4a94000.spi)
[2.905235] geni_spi 4a94000.spi: error -EINVAL: Error applying setting, reverse things back
```

`/dev/spidev*` не появился. Это ожидаемо и безопасно: `msm_pinmux_request()`
отвергает зарезервированные линии **до** записи в TLMM, поэтому SoC не упал.

**Тест 2 — без pinctrl (чтобы дойти до регистров SE):**

SoC **завис** на probe (~2.9 с): экран с подсветкой, USB ACM не поднимается,
устройство пришлось выводить вручную (Power + Vol Down). То есть первое же
обращение `geni-spi` к регистрам `4a94000` (чтение `GENI_IF_DISABLE_RO` /
инициализация SE) не возвращается — блок закрыт для non-secure.

Вывод B: **нет**, ни пины, ни регистры AP недоступны. Ветка 4A закрыта.

---

## 5. Почему 4B дорого (оценка)

В Android путь такой:

```
fingerprint.fpc.default.so ──QSEECom──► fpctzapp (TA) ──qsee_spi(5)──► SE5
        (AIDL HAL)          (libQSEEComAPI + ion)
```

Что есть в mainline:

- `qseecom` работает: `drivers/firmware/qcom/qcom_qseecom.c`,
  `qcom_scm_qseecom_*`; `xiaomi,fog` уже в allowlist (`qcom_scm.c:2338`),
  UEFI secure app поднимается.
- Но интерфейс **минимальный**: только `qcom_scm_qseecom_app_get_id()` +
  `app_send()` (request/response в TZ-памяти) и жёсткий список приложений в
  драйвере. Нет `APP_LOAD` (загрузка TA из `.mdt`/`.b0x`), нет
  `register_shared_buffer`, нет ion — а HAL/TA этим пользуются.
- **Живая проверка (2026-10-27):** временно добавили `fpctzapp` в список
  qseecom-драйвера и залогировали lookup:

  ```
  qcom_qseecom: FP-DIAG qseecom lookup qcom.tz.uefisecapp -> 0 (app_id 2)
  qcom_qseecom: FP-DIAG qseecom lookup fpctzapp -> -2 (app_id 0)
  ```

  То есть TEE **не знает** `fpctzapp`: в Android его грузит ядерный qseecom
  из `/vendor/firmware_mnt/image`, а в mainline загрузчика TA нет. Даже
  «дойти до протокола» нельзя, пока не реализован APP_LOAD.
- Протокол HAL↔TA — сотни команд (`FPC_TA_*`), поверх них токены и
  secure storage (`qsee_fts_*`, `qsee_hmac`, `qsee_km_get_encapsulated_key`).
  Всё это надо реверсить из бинаря HAL и реализовать в своём клиенте.

Оценка: это отдельный исследовательский проект (недели), с неясным
результатом на этапе токенов/аттестации. Соотношение цена/результат плохое,
поэтому по правилу из handoff задача закрывается документом.

Если возвращаться — порядок такой: (1) реализовать APP_LOAD в mainline
qseecom (образ TA из `.mdt` + `.b0x`, SCM-команда загрузки как в Android
`qseecom`), (2) убедиться, что `app_get_id("fpctzapp")` отдаёт id,
(3) только потом реверсить протокол HAL↔TA.

---

## 6. Что сделано в дереве

- `arch/arm64/boot/dts/qcom/sm6225.dtsi`: узел `spi5: spi@4a94000` добавлен
  **disabled** с комментарием-предупреждением (включать нельзя — вешает SoC).
- `arch/arm64/boot/dts/qcom/sm6225-xiaomi-fog.dts`: `&spi5` намеренно **не**
  включается; комментарий со ссылкой сюда.
- `tools/fp-probe.c` — минимальный SPI-пробник FPC1020 (reset через
  gpio-leds + чтение HWID `0xfc`). Оставлен на случай, если появится
  легальный доступ к шине; в initramfs собирается, автоматически не
  запускается.
- `initramfs/display-console-rescue-init` — без изменений (автозапуск
  пробы убран).

Артефакты разбора (не в git, `out/` генерируемый): `out/fp-recon/` —
`fpctzapp.*`, `fingerprint.fpc.default.so`, `tz_a.bin`, `xbl_a.bin`.

---

## 7. Обновление статуса

`STATUS.md` и `docs/hardware-inventory.md`: строка «Сканер отпечатка» теперь
**NOT SUPPORTED (TEE-only)** с ссылкой на этот документ — раньше было просто
«драйвера нет», теперь известно, что драйвер и не поможет: шина не у AP.
