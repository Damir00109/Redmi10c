# Статус bring-up

Последнее обновление: **2026-09-25**  
Устройство для проверки: **Xiaomi Redmi 10C `fog`**, Qualcomm **SM6225/Khaje**.  
Метод загрузки: временный `fastboot boot`, без постоянной прошивки разделов.

## Сводная таблица

| Компонент | Статус | Что именно проверено / ограничение |
|---|---|---|
| Загрузка rescue-ядра | **Работает** | Linux 7.1.5 загружается на `fog`; USB-консоль доступна |
| Android fallback | **Работает** | Android на слоте A сохранён; rescue грузится временно |
| Экран / simplefb-консоль | **Работает** | Вывод rescue-консоли через текущий framebuffer |
| DRM / MDSS / DPU / DSI-драйверы | **Работают** | Драйверы собираются и регистрируются; DPU и DRM-карта поднимаются |
| DSI-панель / panel pipeline | **Работает** | Xinli FT8006S: `card0-DSI-1`, `status=connected`, режим `720x1650`; panel driver, DSI PHY и MDSS/DPU проверены на `fog` |
| USB gadget ACM | **Работает** | Хост видит `/dev/ttyACM0`, доступен shell |
| UFS | **Работает** | Контроллер и накопитель обнаруживаются |
| SD-карта | **Работает** | Контроллер/карта обнаруживаются |
| Touchscreen FocalTech FT8006S | **Работает** | SPI-драйвер и firmware из Android; multitouch events проверены |
| Thermal sensors | **Работает** | Температурные датчики регистрируются |
| Vibrator | **Работает** | Управление вибромотором проверено |
| Зарядка / fuel gauge | **Работает** | Драйверы зарядки и измерения батареи поднимаются |
| SoundWire RX | **Работает** | Khaje frame/порт-параметры и RX-маршрут стабильны |
| Speaker audio | **Работает с дефектом** | Есть воспроизведение без прежней сильной дисторсии; остаётся broadband hiss и небольшой click |
| Audio calibration (ACDB) | **Не реализовано** | Android ACDB/userspace calibration ещё не перенесены |
| RX mute sequencing | **Частично проверено** | Vendor-подобная mute/unmute логика добавлена; тестировать только пустыми файлами |
| Wi-Fi | **Не завершено** | Firmware и базовая инфраструктура присутствуют; полный рабочий цикл не зафиксирован |
| Bluetooth | **Не завершено** | Firmware присутствует; полный рабочий цикл не зафиксирован |
| Adreno 610 kernel init | **Работает** | DRM, SMMU, GMU, ZAP и GPU hw init проходят |
| GPU clock / PLL | **Работает** | Khaje ZONDA PLL0 → OUT_MAIN; вендорные 320/465/600/785/1025/1114.8 МГц доступны |
| GPU devfreq / OPP | **Работает** | `simple_ondemand`, `cur_freq`; таблица приведена к вендорной, предупреждение devfreq убрано |
| GPU real rendering/load | **Не проверено** | `kmscube`/Mesa/freedreno userspace-тест ещё не запускался |
| GPU userspace Vulkan/OpenGL | **Не проверено** | Kernel bring-up подтверждён, полноценный userspace stack не включён |
| Камера | **Не реализовано** | Драйверы и pipeline не поднимались |
| Audio microphone / recording | **Не завершено** | Полный capture path не подтверждён |
| Modem / cellular data | **Не завершено** | Firmware и remoteproc-задел есть; полноценный modem stack не является целью rescue initramfs |

## Что означает статус

- **Работает** — проверено на реальном `fog` в rescue-образе.
- **Работает с дефектом** — основной путь функционирует, но известна проблема, указанная в таблице.
- **Не завершено** — часть kernel-side инфраструктуры может быть готова, но полный hardware/userspace цикл не подтверждён.
- **Не проверено** — намеренно не заявляется как рабочее до отдельного теста.
- **Не реализовано** — bring-up ещё не проводился.

## Рабочие артефакты

- `boot-GOOD8-gpu-full.img` — локальный образ с рабочим GPU kernel bring-up.
- `docs/GPU-BRINGUP.md` — подробности GPU.
- `docs/audio/NOISE-ANALYSIS.md` — расследование шума в аудио.
- `tools/fetch-firmware.sh` — загрузка закрытых firmware из release asset.
