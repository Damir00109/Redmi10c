# План работ: native Linux **7.1** на Redmi 10C (rain)

**Цель:** нативное ядро **Linux 7.1** (не Android 4.19), телефон как Linux-машина.  
Сейчас рабочий плацдарм — Statzar **v6.1** (уже boot+дисплей); дальше перенос на **7.1** / актуальный mainline той же линии.  
**Устройство:** rain (NFC), SM6225 / Khaje, Lineage 23.2 запасной слот  
**Правило безопасности:** только `fastboot boot` → откат; не трогать `abl`/`xbl`/GPT  
**Отладка:** USB-only (UART нет) + экран  

---

## Цель по этапам (что считаем успехом)

| Этап | Успех |
|------|--------|
| 0 | Парашют + деревья + сборка Image на ПК |
| 1 | Ядро грузится временно (`fastboot boot`), виден UART/adb/shell |
| 2 | Экран (simplefb или DRM) + USB + накопитель |
| 3 | Тач |
| 4 | Wi‑Fi / BT (по возможности) |
| 5 | Аудио / модем / камера / FP — по желанию |
| 6 | **NFC — в последнюю очередь** (после всего остального, что решим поднимать) |

Lineage на слоте остаётся «домашним» миром. Mainline — эксперимент рядом.

---

## Фаза 0 — страховка и база (почти сделано)

**Статус:** ~90%

- [x] Unlock + Magisk, телефон подопытный
- [x] ПК: adb/fastboot, toolchain, ccache, mkbootimg
- [x] Бэкап `super` + sensitive (persist/modemst/fsg)
- [x] A/B-дамп + `backup/.../active/` (boot/dtbo/vendor_boot/…)
- [x] Выгрузка firmware / NFC conf / `boot_b`+`dtbo_b`
- [x] Декомпил rain overlay; панель = **ft8006s xinli** (`c3q_43_03_0b`)
- [x] Репы: Statzar v6.1 (fog.dts), SCOS100 v6.19-rc8; скелет `sm6225-xiaomi-rain.dts`
- [ ] Дождаться окончания ночной сборки → проверить `out/*/Image.gz` + `*.dtb`
- [ ] Один раз проверить откат: `fastboot flash boot active/boot.img` (пока телефон живой)

**Критерий выхода:** Image.gz есть; откат Lineage проверен; GitLab GoldenVadim не нужен (404).

---

## Фаза 1 — первый boot mainline

**Статус: УСПЕХ (2026-07-26)** — `fastboot boot` fog TEST:
- экран (simplefb) работает
- initramfs крутит `rain-mainline: alive`
- USB ACM на хосте пока нет (нет полного USB в DT)
- отладка: USB-only / экран (UART нет)

**Важно:** после теста `dtbo` стёрт → перед Lineage запустить `tools/restore-android.sh` из fastboot.

---

## Фаза 2 — bringup железа (завершена 2026-08-10)

Результат: `boot-linux-final.img` — рабочий плацдарм с USB RNDIS/ACM/ADB, UFS, simplefb, touch, thermal, Type-C.

Порядок по сложности (из `HARDWARE-DRIVERS.md`):

| # | Блок | Источник правды | Сложность |
|---|------|-----------------|-----------|
| 1 | Clocks / pinctrl / UART | sm6225.dtsi upstream | средне |
| 2 | UFS / userdata read | qcom ufs | средне |
| 3 | USB gadget (adb/rndis) | dwc3 | средне |
| 4 | Simple-framebuffer → DRM/DSI | fog DT + panel ft8006s | средне |
| 5 | Регуляторы / PMIC | Android DT | средне |
| 6 | Тач Focaltech SPI | FW `focaltech_ts_fw_xinli.bin` | средне+ |
| 7 | GPU Adreno 610 | `a610_zap*` + msm/freedreno | **✅ ГОТОВО** |
| 8 | Wi‑Fi/BT qcacld/WCN | firmware_mnt | тяжело |
| 9 | Аудио / модем / камеры / FP | HAL+реверс | опционально |
| 10 | **NFC NXP** (`nq@2a`, conf уже сняты) | `libnfc-nxp*.conf` + overlay | **последним** |

Каждый пункт: правка DT → сборка на ПК → `fastboot boot` → лог → откат.

---

## Фаза 3 — жизнь на Linux (когда shell стабилен)

- Rootfs: postmarketOS / Arch ARM / Debian rootfs на SD или отдельный раздел userdata  
- Не сносить Lineage, пока mainline не грузится сам  
- Dual-boot: Lineage = slot boot по умолчанию; mainline = ручной `fastboot boot` или отдельный boot-слот только после уверенности  

---

## Фаза 4 — цель native **Linux 7.1**

- Сначала добить bringup на 6.x (USB gadget, UFS, тач…) — уже есть boot+дисплей
- Перенести DT fog/rain на **Linux 7.1** (или 7.1-rc → релиз)
- SCOS `v6.19-rc8` — промежуточная ступень, не финал
- Апстрим DT/драйверов уже под 7.1
- NFC — по-прежнему последним

---

## Что не делаем (анти-план)

- Не флешить `abl`/`xbl`/`modem`/`persist` без отдельного решения  
- Не `fastboot oem lock`  
- Не EDL «на удачу»  
- Не ждать полный паритет с Lineage (камеры/модем — бонус)

---

## Следующие шаги (безопасные — без тебя не бучу)

1. ~~GPU Adreno 610 (`a610_zap*`) без включения mdss/dispcc.~~ **✅ ГОТОВО (2026-08-14)** — Vulkan Turnip работает.
2. Wi-Fi: добить soft-hang ~60s (ath10k_snoc / NAPI / RX refill).  
3. Bluetooth / WCN3990 BTFM (share FW с Wi-Fi).  
4. ~~Display DRM/DSI + панель `c3q_43_03_0b`~~ **✅ ГОТОВО** — FT8006S 720x1650, fbcon, backlight.
5. Sensors / audio / camera / modem — по желанию, после основного.  
6. NFC — в последнюю очередь, как и планировалось.

---

## Артефакты в репо ПК

| Путь | Зачем |
|------|--------|
| `PLAN.md` | этот план |
| `STATUS.md` / `HOST-SPECS.md` / `HARDWARE-DRIVERS.md` | статус, железо, хост |
| `backup/ab-*/active/` | мгновенный откат Lineage |
| `mainline/linux-sm6225` | v6.1 + fog/rain DT |
| `mainline/sm6225-mainline` | v6.19-rc8 |
| `tools/build-*.sh` `pack-boot-test.sh` | сборка/паковка без flash |
| `dt/key/RAIN-*.dts` | Android overlay как справочник |

---

## Оценка трудозатрат (честно)

| До | Ориентир |
|----|----------|
| Первый boot/panic лог | 1–3 вечера |
| Экран + USB shell | 1–3 недели |
| Тач | ещё недели |
| NFC | в самом конце, после Wi‑Fi/остального |
| Wi‑Fi usable | месяцы / как повезёт |
| «Как телефон на Linux» | длинный порт, не спринт |

План живой: после первого `fastboot boot` фаза 2 уточняется по логам.
