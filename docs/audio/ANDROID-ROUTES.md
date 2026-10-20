# Android: точные маршруты по сценариям (2026-10-20)

Снято на живом устройстве (LineageOS, Android 17) фоновым сэмплером
`mixdump` (свой тул на tinyalsa, печатает **индексы** контролов — у tinymix
enum'ы печатаются без разделителей и неоднозначны).

Сценарии выполнялись вручную: динамик → пауза → наушники → пауза →
микрофон. Ниже — только то, что реально менялось в каждой фазе.

## 1. Динамик (музыка, 21:09:16)

| Контрол | Значение | Смысл |
|---|---|---|
| `RX_CDC_DMA_RX_1 Audio Mixer MultiMedia1` | `1` | FE MultiMedia1 → BE RX_CDC_DMA_RX_1 |
| `RX_MACRO RX2 MUX` | `2` | RX2 ← AIF2_PB |
| `RX INT2_1 MIX1 INP0` | `7` | вход микшера INT2 ← RX2 |
| `AUX_RDAC Switch` | `1` | ЦАП AUX включён |
| `AUX PATH Mode` | `0` | NORMAL_MODE |
| `aw87xxx_profile_switch_0` | `0` | профиль Music |
| `aw87xxx_spk_switch` / `aw87xxx_rcv_switch` | `0` | выход на динамик |

Итог: **DSP → RX_CDC_DMA_RX_1 → RX macro AIF2 → RX2 → INT2_1 → AUX_RDAC →
AUX_OUT (аналог) → AW87xxx → динамик**.

## 2. Наушники (вставлены, 21:09:35–21:09:41)

| Контрол | Значение | Смысл |
|---|---|---|
| `RX_CDC_DMA_RX_0 Audio Mixer MultiMedia1` | `1` | FE → **другой** BE: RX_CDC_DMA_RX_0 |
| `RX_MACRO RX0 MUX` / `RX1 MUX` | `1` / `1` | RX0 и RX1 ← AIF1_PB |
| `RX INT0_1 MIX1 INP0` | `5` | INT0 ← RX0 |
| `RX INT1_1 MIX1 INP0` | `6` | INT1 ← RX1 |
| `RX INT0 DEM MUX` / `RX INT1 DEM MUX` | `1` / `1` | CLSH_DSM_OUT |
| `HPHL_RDAC Switch` / `HPHR_RDAC Switch` | `1` / `1` | ЦАПы наушников |
| `HPHL_COMP Switch` / `HPHR_COMP Switch` | `1` / `1` | компенсация |
| `RX_COMP1 Switch` / `RX_COMP2 Switch` | `1` / `1` | |
| `HPH Type` | `2` | тип нагрузки |
| `HPHL Impedance` / `HPHR Impedance` | `54` / `56` | измеренный импеданс |
| `AUX_RDAC Switch` | `0` | динамик выключен |

Итог: **FE → RX_CDC_DMA_RX_0 → AIF1 → RX0/RX1 → INT0/INT1 → HPHL/HPHR**.
Важно: у наушников **другой BE** (`RX_CDC_DMA_RX_0`), не тот, что у динамика.

## 3. Микрофон (диктофон, 21:10:40)

| Контрол | Значение | Смысл |
|---|---|---|
| `MultiMedia1 Mixer TX_CDC_DMA_TX_3` | `1` | FE → BE TX_CDC_DMA_TX_3 |
| `TX DEC0 MUX` | `1` | DEC0 ← MSM_DMIC |
| `TX SMIC MUX0` | `1` | SMIC ← SWR_MIC0 |
| `TX_AIF1_CAP Mixer DEC0` | `1` | AIF1 CAP ← DEC0 |
| `ADC1_MIXER Switch` | `1` | аналоговый ADC1 в микшер |

Итог: **DMIC/SWR_MIC0 → DEC0 → TX_AIF1_CAP → TX_CDC_DMA_TX_3 → FE**.

## 4. Разговорный динамик (earpiece)

В этой сессии не снимался — не было звонка. Контролы для него в дереве есть
(`EAR_RDAC Switch`, `EAR_PA Volume`), маршрут по вендорскому
`mixer_paths.xml` (см. `docs/audio/mixer_paths.xml`) — через `EAR_RDAC` и
`EAR_PA`, вход INT1/INT0 (как у наушников), но с BE `RX_CDC_DMA_RX_0`.
Нужен отдельный сценарий со звонком.

## Сверка с нашим mainline-ядром

| Контрол (Android) | У нас |
|---|---|
| `RX_CDC_DMA_RX_0/1 Audio Mixer MultiMedia1` | **есть** (у нас имя `RX_CODEC_DMA_RX_*`) |
| `RX_MACRO RX0/RX1/RX2 MUX` | есть |
| `RX INT0_1/INT1_1/INT2_1 MIX1 INP0` | есть |
| `AUX_RDAC`, `AUX PATH Mode`, `HPHL/HPHR_RDAC`, `HPH*_COMP`, `RX_COMP*`, `HPH Type` | есть |
| `EAR_RDAC`, `EAR_PA Volume` | есть |
| `ADC1_MIXER`, `TX SMIC MUX0` | есть |
| `TX DEC0 MUX`, `TX_AIF1_CAP Mixer DEC0` | **НЕТ** |
| `aw87xxx_*` (профиль/выход PA) | **НЕТ** (у нас отдельный misc-драйвер с sysfs) |

Вывод: **основные маршруты в нашем ядре есть**, но их никто не включает —
PulseAudio не знает про эти контролы и оставляет всё в `off`. Отсюда «звук
не работает». Два реальных пробела: `TX DEC0 MUX` / `TX_AIF1_CAP Mixer DEC0`
(захват) и управление AW87xxx из ALSA.

## Расследование mainline-стороны (2026-10-20)

Попытка включить вендорский маршрут динамика вручную на нашей карте.

### Что применили

```
RX_CODEC_DMA_RX_1 Audio Mixer MultiMedia1 = 1
RX_MACRO RX2 MUX = AIF2_PB        (наш кодек: AUX питается от RX3 → пробовали и AIF1/RX3)
RX INT2_1 MIX1 INP0 = RX2
AUX_RDAC = 1
AUX PATH Mode = NORMAL_MODE
AW87xxx: echo music > /sys/bus/i2c/devices/2-0058/mode
```

### Что получилось

- `amixer` показывает все контролы в нужном состоянии
- `aplay -D hw:0,0 tone.wav` → **несколько записей проходят, затем `write error: Input/output error`**
- в dmesg **ни одной ошибки**
- DAPM: `MM_DL1: Off`, `RX_CODEC_DMA_RX_1 Audio Mixer: Off in 0 out 0` —
  **путь FE→BE не активируется**, DSP перестаёт забирать данные → буфер
  переполняется → EIO
- `speaker-test` вообще не открывает PCM (молча «играет» в никуда)

### Что исключено

| Проверка | Результат |
|---|---|
| ADSP | `remoteproc1: running`, fastrpc-сервисы активны |
| SoundWire | `sdw:2:0:…:4` и `sdw:3:0:…:3` есть, кодек привязан |
| Маппинги портов (`qcom,rx-port-mapping`/`rx-channel-mapping`) | совпадают с апстримным IDP |
| Громкости | RX2/RX3 = 84 (0 dB) |
| DAI-индекс rx-макроса | проверен: 1 = AIF2, соответствует вендору |
| IDP-вариант (BE `RX_0` + AIF1) | тот же EIO |

### Ключевая находка про апстрим

Апстримный `qcm6490-idp.dts` (тот же кодек wcd937x) устроен **принципиально
иначе**: у него нет FE-линков вообще, весь звук идёт через **AudioReach
(`q6apm`/`q6apmbedai` поверх GPR)**, а не через legacy `q6afe`/`q6asm`/`q6routing`.

**Но на нашем устройстве это неприменимо:** прошивка ADSP отдаёт только
APR-каналы (`apr_audio_svc`, `apr_voice_svc`), канала `adsp_apps` для GPR нет.
Значит AudioReach без замены прошивки недоступен, и чинить нужно legacy-путь.

### Ещё два пробела, найденные по ходу

1. **FS1599 не привязан**: `2-0034` без драйвера (`fs210x.c` матчит только
   `foursemi,fs2105s`). Это smart-boost PVDD для усилителя динамика.
   Прошивка есть (`initramfs/lib/firmware/foursemi/fs1599.fsm`, 416 Б) — но это
   блоб для внутреннего DSP чипа, не таблица регистров.
2. **AUX в нашем кодеке питается от RX3**, а не от RX2 как у вендора
   (`RDAC4 ← RX3` в `wcd937x.c`); у вендора есть мультиплексор.
