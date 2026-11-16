# Redmi 10C (rain/fog) — аудио-тракт: собранная информация

Источники: живой DT (`/sys/firmware/fdt` → `android-live.dts`), `mixer_paths.xml`,
dmesg/Android, `/proc/asound`, sysfs i2c.

## Два устройства в цепочке громкоговорителя

| Чип | Шина/адрес | DT-compatible | Роль | Firmware |
|---|---|---|---|---|
| **Awinic AW87xxx** (chipid 0x76, pid_76) | i2c-0 `0x58` (`aw87xxx_pa_58@58`) | `awinic,aw87xxx_pa` | Усилитель динамика (аналоговый вход!) | `/vendor/firmware/aw87xxx_acf.bin` (1374 B, init-таблица) |
| **FourSemi FS1599** | i2c-0 `0x34` (`fs15xx@34`) | `foursemi,fs15xx` | Smart-boost PVDD для PA | `/vendor/firmware/fs1599.fsm` (416 B) |

Обе ноды в DT минимальные: `reg`, `dev_index=0`, `status=okay` — без gpio/supply,
вся логика в вендорных драйверах (`audio_aw87xxx`, `audio_fs1599` модули).

НЕ путать: `awinic@64` (`awinic,aw2016_led`, disabled) — RGB-индикатор, не аудио.

## Архитектура звука

```
ADSP (avs/audio) ──soundwire──> WCD937x codec ("bolero", PMIC-adjacent)
   rx-macro@a600000 rx_swr_master  (swr_master_id=2, 5 портов)
   tx-macro@a620000               (микрофоны)
   va-macro@a730000 va_swr_master (swr_master_id=3, always-on/DMIC)
   wcd937x-tx-slave  reg 0x0a 0x1170223
   wcd937x-rx-slave  reg 0x0a 0x1170224

RX2 → RX INT2_1 MIX1 → AUX_RDAC → AUX_OUT (аналог!)
                                     │
                                     v
                              AW87xxx PA (0x58)
                              + FS1599 boost (0x34) → PVDD
                                     │
                                     v
                               ГРОМКОГОВОРИТЕЛЬ
```

**Ключевое:** динамик НЕ на I2S/TDM и НЕ на SWR-WSA — PA аналоговый,
питается от AUX-выхода кодека. `wsa881x-i2c-codec@44` в DT = `disable`.

## Другие выходы кодека (второй "динамик" = разговорный)

- `EAR_RDAC` / `EAR PA GAIN` / `RX_EAR Mode` — earpiece (прямой выход rx-macro)
- `IN1_HPHL/IN2_HPHR → HPHL/HPHR_OUT` — наушники; через **FSA4480** (i2c 0x42,
  USB-C analog audio switch, `qcom,fsa4480-i2c`)
- MBHC: `msm-mbhc-hphl-swh=1`, `msm-mbhc-gnd-swh=1` — детекция гарнитуры/кнопок

## Mixer (mixer_paths.xml)

Путь `speaker`:
```
RX_CDC_DMA_RX_1 Channels = One
RX_MACRO RX2 MUX         = AIF2_PB
RX INT2_1 MIX1 INP0      = RX2
AUX_RDAC Switch          = 1
RX_RX2 Digital Volume    = 79
aw87xxx_spk_switch       = Music   (Off/Music/Voice/Fm — контроль PA-драйвера)
AUX PATH Mode            = HP_MODE
```

Звуковая карта: `bengal-idp-snd-card` (`qcom,bengal-asoc-snd`),
`asoc-codec-names = "msm-stub-codec.1", "bolero_codec"`.

## Что нужно для mainline

| Компонент | В mainline | Статус |
|---|---|---|
| WCD937x codec (wcd937x.c + sdw + rxmacro/txmacro/vamacro, wcd-mbhc) | ✅ есть (>=6.x, sm8450+) | портировать sm6225 + bolero-v3 |
| SoundWire host (`qcom,swr-mstr`) | ✅ есть | рег-адреса из live.dts |
| LPASS CDC-DMA DAIs | ✅ есть | — |
| Soundcard | ⚠️ нужен вариант под sm6225/bengal | по образцу `qcom,sm6115-sndcard` |
| **AW87xxx PA** | ❌ нет upstream | минимальный драйвер: replay init из `aw87xxx_acf.bin` + режимы Off/Music/Voice |
| **FS1599 boost** | ❌ нет upstream | вероятно минимум enable+вольтаж; тест: без него PA может играть тише/молчать |
| FSA4480 | ✅ `sound/soc/codecs/fsa4480.c` | USB-C аналоговая гарнитура |

Данные: ADSP-прошивка уже умеет грузиться (PAS+PDR стек сделан для WCN3990 —
тот же рецепт, сервисные .jsn для adspr.jsn уже в initramfs).

## Файлы

- `docs/audio/android-live.dts` — полное живое DT Android (491 KB)
- `docs/audio/mixer_paths.xml` — вендорные микшеры
- `initramfs/lib/firmware/awinic/aw87xxx_acf.bin` — init PA (1374 B)
- `initramfs/lib/firmware/foursemi/fs1599.fsm` — конфиг буста (416 B)

## Открытые вопросы

- ~~Точный чип AW87xxx~~ — **решено**: chipid `0x76` → семейство pid_76
  (`aw87390` / `aw87320` / `aw87401`, рег-таблица `aw87xxx_pid_76_reg.h`
  в вендорном `techpack/audio/asoc/codecs/aw87xxx`). Точную модель
  различает ACF-профиль.
- ~~Нужен ли fs15xx~~ — **решено: НЕ нужен, чип не распаян/не отвечает**.
  Проверено на живом Android (2026-11):
  - i2c `0-0034` **UNBOUND** — драйвер `fs15xx` зарегистрирован
    (`/sys/bus/i2c/drivers/fs15xx` есть), но устройство не пробуется;
  - ручной `echo 0-0034 > .../fs15xx/bind` → `i2c error -107` (NACK) на
    всех рег-пробах, `fsm_detect_device: invalid dev_id:00`,
    `probe failed -5` — чип молчит на шине;
  - `/proc/misc` без `fs15xx`, `/dev/fs15xx` отсутствует;
  - `fs1599_dlkm` загружен только как provider символов
    (`aw_check_dsp_ready`, `aw_send_afe_cal_apr`) для `aw87xxx_dlkm` и
    `machine_dlkm` — реального устройства за ним нет;
  - Android-звук при этом работает штатно.
  Вывод: нода `fs15xx@34` в DT — для другого SKU; на этом экземпляре
  FS1599 либо не распаян, либо обесточен. **В Linux драйвер не нужен.**
  Вендорный исходник для справки: `techpack/audio/asoc/codecs/fs1599/`
  в `CHRISL7/kernel_xiaomi_sm6225` (FSM v4.0.5, `CONFIG_FSM_Q6AFE` off —
  чип умел бы I2S-вход с собственным DSP защиты, но на плате его нет).
- Напряжение буста и sequence включения PA↔boost — неактуально (fs15xx нет).
- Держит ли AUX-выход кодека постоянный idle-шум (mute-поведение)
