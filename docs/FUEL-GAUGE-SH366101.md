# Fuel gauge SH366101 — полный драйвер

Дата: 2026-10-18. **Статус: работает полностью (SBS + расширенные gaugeinfo-блоки).**

Драйвер: `drivers/power/supply/sh366101_fg_bringup.c`
(Kconfig `CONFIG_FUEL_GAUGE_SH366101_BRINGUP`), DT-нода `sh366101@55` на `&i2c1`,
регистрирует power_supply `bms`. Зарядник SMB1351 — отдельный `battery`.

## Что отдаётся в sysfs (`/sys/class/power_supply/bms/uevent`)

| Свойство | Источник | Пример |
|---|---|---|
| `STATUS` | SBS 0x06 (bat_flags) | Full / Charging / Discharging |
| `HEALTH` | SBS 0x06 (temp-флаги) | Good / Overheat / Cold |
| `PRESENT` | SBS 0x06 bit3 | 1 |
| `CAPACITY` | SBS 0x1C | 100 |
| `VOLTAGE_NOW` | SBS 0x04 | 4442000 µV |
| `CURRENT_NOW` | SBS 0x10 | 177000 µA |
| `TEMP` | SBS 0x02 (NTC батареи) | 335 (33.5 °C) |
| `CHARGE_FULL` | SBS 0x0E (FCC) | 4780000 µAh |
| `CHARGE_NOW` | SBS 0x0C (RMC) | 4750000 µAh |
| `CYCLE_COUNT` | SBS 0x1A | 41 |
| `CHARGE_FULL_DESIGN` | BLOCK2 PackQmax | 5096000 µAh |
| `ENERGY_FULL` | BLOCK5 TrueFCE | 20254000 µWh |
| `ENERGY_NOW` | BLOCK5 TrueRE | 20224000 µWh |
| `VOLTAGE_OCV` | BLOCK6 OcvVcell | 4418000 µV |
| `TEMP_AMBIENT` | CALIINFO IntTemper (кристалл) | 342 (34.2 °C) |
| `TECHNOLOGY`, `MODEL_NAME` | константы | Li-ion, sh366101 |

SOH для upower/GNOME = `CHARGE_FULL / CHARGE_FULL_DESIGN` = 4780/5096 ≈ **93.8 %**.

SBS-регистры обновляются каждые 5 с, gaugeinfo-блоки — каждые 30 с
(4 блок-чтения по ~10 мс, отдельный интервал, чтобы не грузить шину).

Диагностика: `mount -t debugfs none /d && cat /d/sh366101/dump` —
дамп всех блоков с полями, включая FusionModel (импедансная модель).

## Протокол блоков (из downstream `sh366101_fg.c`)

Команды — 32-битные, маска в старшем байте (`CMDMASK_ALTMAC_R = 0x08000000`),
но в регистр пишутся только младшие 16 бит:

```
запись  cmd        -> ALTMAC  0x3E   (i2c_smbus_write_word_data)
пауза   3 мс
чтение  32 байта   -> ALTBLOCK 0x40  (combined write-reg + read, i2c_transfer)
пауза   3 мс
чтение  checksum   -> ALTCHK  0x60   (i2c_smbus_read_word_data)
        len = (chk >> 8) - 4;  sum = cmd_lo + cmd_hi + data[0..len-1];  ~sum == chk_lo
```

| Команда | Значение | Содержимое |
|---|---|---|
| CALIINFO | 0xE014 | калибровка, IntTemper (внутр. темпа) |
| GAUGEINFO | 0xE0 | Run/State/Work/RSTS |
| GAUGEBLOCK2 | 0xE2 | PackQmax, CycleCount, VatEOC/IatEOC/ChgVEOC, RelaxCycle |
| GAUGEBLOCK3 | 0xE3 | PasCol/PasEgy (кулоновский счётчик) |
| GAUGEBLOCK4 | 0xE4 | сырые RC/FCC/RE/FCE, C0DOD0 |
| GAUGEBLOCK5 | 0xE5 | FilRC/FilFCC/FSOC, TrueRC/TrueFCC/RSOC, TrueRE/TrueFCE (энергия) |
| GAUGEBLOCK6 | 0xE6 | ChgVol/ChgCur/TapCur, OcvVcell, SOCJmp |
| GAUGEBLOCK7 | 0xE7 | служебный блок |
| FUSION | 0xEF | FusionModel — 16 слов импедансной модели |

**Unseal не нужен** — все блоки читаются на запечатанном гейдже (проверено).
Downstream-овский `CMD_UNSEALKEY` (0x80008000) для чтения не требуется.

## Платформенный quirk: SE-DMA на QUPv2 вешает шину

Любой i2c-transfer **≥ 32 байт** уходит в SE-DMA
(`geni_i2c_rx_one_msg()` → `i2c_get_dma_safe_msg_buf(msg, 32)`; при
`len == 32` порог срабатывает, т.к. условие `len < threshold`) и на Khaje
**никогда не завершается**: transfer не возвращает ошибку, задача уходит в
D-state, система ловит hung-task (без `hung_task_panic` — просто мёртвый
shell; с паникой в других путях — ребут). Именно так выглядели первые
попытки чтения блоков: запись ALTMAC (`rc=0`), чтение checksum (2 байта,
FIFO) — работают, а 32-байтное чтение — зависание.

Бисект на железе:

| Операция | Размер | Режим | Итог |
|---|---|---|---|
| SBS word read (0x04 и др.) | 2 B | FIFO | ok |
| ALTMAC write | 3 B | FIFO | ok (rc=0) |
| ALTCHK read | 2 B | FIFO | ok (0x24bb) |
| ALTBLOCK read | **32 B** | **SE-DMA** | **hang** |

**Фикс:** DT-property `qcom,no-dma` в `i2c-qcom-geni.c` пиннит шину к FIFO
(`gi2c->no_dma = true`), в `sm6225-xiaomi-fog.dts` оно выставлено на `&i2c1`.
После фикса: `blk rc=0`, checksum совпадает (`sum_ok=1`), шина стабильна.

## Валидация (kernel #26, rescue, 2026-10-18)

BLOCK2 против живого дампа стокового драйвера (`docs/audio/capture/pcm_hw.txt`):

| Поле | Наше | Сток |
|---|---|---|
| PackQmax | 5096 | 5096 |
| VatEOC | 4408 | 4408 |
| IatEOC | 180 | 180 |
| ChgVEOC | 4450 | 4450 |
| CycleCount | 41 | 37 |

BLOCK5/BLOCK6: TrueFCC 4849 мАч, TrueRE 20224 мВт·ч, OcvVcell 4418 мВ,
ChgCur 3600 мА, TapCur 200 мА — совпадает со стоком (4813 / 19614 / 4416 /
3600 / 200). Контрольные суммы всех блоков валидны.

Стабильность: 4+ мин аптайма, load 0.14, **0** сообщений
i2c timeout/error/abort, значения обновляются живьём.

## Ограничения

- Прошивка AFI/TS в гейдж не загружается — предполагается, что он уже
  запущен загрузчиком (как в стоке).
- Не экспонируются: lifetime-ADC (мин/макс за всю жизнь), EOC-история
  отдельными свойствами, FusionModel — доступны через debugfs-дамп, но не
  как power_supply-свойства (нет подходящих стандартных имён).
- Раскладка полей BLOCK3/BLOCK4 сверена частично (значения правдоподобны,
  но эталонного дампа под них в стоковом логе меньше).
