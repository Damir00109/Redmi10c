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
