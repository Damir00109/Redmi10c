# План обновления на Linux 7.3

Текущая база: **Linux 7.1.5** + `kernel.patch` (94 файла: 69 правок апстрима,
12 наших новых файлов/каталогов, ~1635 вставок).

Версия задаётся в `build.sh`: `KERNEL_TAG=v7.1.5`, `KERN_OUT=…/linux-7.1.5`.

## Стратегия

Обновляем **сначала `stable`** (там нет аудио — меньше движущихся частей), и
только когда 7.3 стабильно грузится и проходит тест-матрицу, переносим на неё
ветку `kernel` с аудио.

```
pre-stable-2026-10-21 ──┬── stable ──(7.3)── stable-7.3
                        └── kernel ──(rebase на stable-7.3)── kernel-7.3
```

## Шаг 0. Подготовка (без риска)

1. `git checkout stable && git checkout -b kernel-7.3-prep` — рабочая ветка
2. Убедиться, что бэкап свежий: `/mnt/931/Redmi10CBackups/backup-2026-10-21`
3. На телефоне оставить `boot_b` на 7.1.5 — это точка отката

## Шаг 1. Подмена версии

```sh
sed -i 's/v7\.1\.5/v7.3/; s/linux-7\.1\.5/linux-7.3/' build.sh
```
Версия прописана явно ещё в пяти местах — их надо поправить все:

| Файл | Строки |
|---|---|
| `build.sh` | 12 (`KERNEL_TAG`), 23 (`KERN_OUT`) |
| `tools/rebuild-rescue.sh` | 7, 14, 16 (путь `out/build/linux-7.1.5`) |
| `rootfs/tools/pack-boot-linux.sh` | 14 |
| `rootfs/build-rootfs.sh` | 154, 155, 158, 160, 161 |

Проверка после правки:
```sh
grep -rn "linux-7\.1\.5\|v7\.1\.5" --include="*.sh" . | grep -v "^./out/"
```

## Шаг 2. Перенос патча

```sh
git clone --branch v7.3 --depth 1 … out/src/linux-7.3
cd out/src/linux-7.3 && git apply -3 ../../kernel.patch   # или patch -p1 --merge
```

Дальше — по группам, от простого к сложному.

## Шаг 3. Порядок разбора конфликтов

### Группа A — новые файлы, конфликтов не бывает (12)

`sm6225.dtsi`, `sm6225-xiaomi-{fog,rain,fog-sm6115}.dts`, `pinctrl-khaje.c`,
`sh366101_fg.c`, `smb1351_charger.c`, `ov02b1b.c`, `aw87xxx-mini.c`,
`wl2866d.c`, `panel-xinli-ft8006s.c`, `fts_spi/`, `dwc3-qcom-legacy.c`.

**Проверить:** не появились ли в 7.3 апстримные аналоги (см. шаг 4).

### Группа B — косметика логов (тривиально, 6)

`block/partitions/core.c`, `drivers/base/core.c`, `drivers/regulator/core.c`,
`drivers/scsi/{scsi_scan,sd}.c`, `drivers/remoteproc/qcom_q6v5.c` —
всё `pr_info/pr_err` → `pr_dbg`. Конфликты решаются вручную за минуту.

### Группа C — средний риск (12)

| Файл | Наши строки | Что смотреть |
|---|---|---|
| `drivers/pinctrl/qcom/pinctrl-msm.c` | ~50 | `reserved_gpios` + `irq_quiesce` — апстрим активно правит probe |
| `drivers/i2c/busses/i2c-qcom-geni.c` | 6 | `qcom,no-dma` |
| `drivers/misc/fastrpc.c` | ? | правки под ADSP |
| `drivers/soc/qcom/{mdt_loader,qcom_pd_mapper,ubwc_config}.c` | ? | |
| `drivers/phy/qualcomm/phy-qcom-{qusb2,snps-femto-v2}.c` | ? | |
| `drivers/usb/dwc3/{core,gadget}.c` | ? | |
| `drivers/interconnect/qcom/{icc-rpm,sm6115}.c` | 16 | |
| `drivers/media/i2c/{hi556,s5kjn1,dw9768}.c` | 35 | |
| `drivers/gpu/drm/msm/dsi/phy/{dsi_phy,dsi_phy_7nm}.c` | 10 | |
| `drivers/net/wireless/ath/ath10k/wmi.c` | 1 | freq==0 guard — может уже быть наверху |

### Группа D — высокий риск (7)

| Файл | Наши строки | Почему риск |
|---|---|---|
| `drivers/clk/qcom/gcc-sm6115.c` | 123 | таблицы клоков, апстрим их правит |
| `drivers/media/platform/qcom/camss/camss.c` | 217 | SM6225 CAMSS — возможно уже добавили |
| `camss-csiphy-3ph-1-0.c` | 111 | lane-таблицы sm6225 |
| `sound/soc/codecs/wcd937x.c` | 165 | кодек, активная разработка |
| `sound/soc/qcom/qdsp6/q6core.c` | 138 | (только на ветке `kernel`, не в `stable`) |
| `drivers/bluetooth/hci_qca.c` | 89 | speed re-apply |
| `drivers/clk/qcom/gpucc-sm6375.c` | 82 | Zonda PLL |
| `drivers/ufs/{core/ufshcd,host/ufs-qcom}.c` | ? | quirk'и, апстрим активен |

Плюс `drivers/clk/clk.c` — наши три фикса (orphan reparent). **Высокий шанс,
что часть уже в апстриме** — сверить с `git log v7.1.5..v7.3 -- drivers/clk/clk.c`
и, если принято, выкинуть наш вариант.

## Шаг 4. Сверка с апстримом (до разбора конфликтов!)

Для каждого «нашего» изменения проверить, не появилось ли оно в 7.2/7.3:

```sh
git log --oneline v7.1.5..v7.3 -- <файл>
git log --oneline v7.1.5..v7.3 --grep="sm6225\|khaje\|wcd937\|wcd937x"
```

Особенно интересны: `sm6225` (DT и CAMSS), `pinctrl-khaje`, `q6dsp`,
`wcd937x`, `fs15xx`. Всё, что уже наверху — **выбрасываем своё**, это
уменьшит патч.

## Шаг 5. Сборка

```sh
rm -rf out/src/linux out/build/linux-7.1.5     # чистое дерево
./build.sh                                     # или tools/rebuild-rescue.sh
```

Конфиг: `kernel.config` переносится как есть, затем
`make olddefconfig` — **проверить дифф** новых `CONFIG_*` (могут появиться
обязательные опции, а что-то — исчезнуть).

## Шаг 6. Тест-матрица (на железе, `fastboot boot`!)

1. Загрузка: `systemd is-system-running = running`, **0 failed units**
2. `dmesg`: 0 WARN/Oops (эталон — `docs/baseline-2026-10-20.txt`, если есть)
3. Wi-Fi (`wlan0` с адресом), BT (`hci0 UP RUNNING`)
4. Камера: задняя (4080×3072) + передняя (2592×1944) по рецепту из STATUS
5. Хранилище: UFS корень, SD-карта ro/rw
6. Зарядка + fuel gauge (`bms`: V/I/SOC)
7. Suspend/resume: `suspend_stats success=1, fail=0`
8. Модем: `mmcli -m 0` видит, LTE поднимается
9. NFC/depth/аудио — не проверяем (в `stable` их нет)

Только после прохождения — `fastboot flash boot_b`.

## Шаг 7. Перенос аудио-ветки

```sh
git checkout kernel
git rebase --onto stable-7.3 pre-stable-2026-10-21 kernel
```
Конфликты будут в `sound/soc/*` и `q6dsp` — там наши правки самые большие.
После — заново проверить, что карта регистрируется (даже если звук не играет,
она должна подниматься без WARN).

## Откат

- Тег `pre-7.3` перед началом
- Образ 7.1.5 лежит в `out/boot_b-stable-noaudio.img` и в бэкапе
- `fastboot flash boot_b` из бэкапа возвращает всё за минуту

## Оценка

| Этап | Время |
|---|---|
| Шаг 1–2 (версия + apply) | ~15 мин |
| Шаг 4 (сверка с апстримом) | ~30 мин, экономит часы |
| Группы A–C | ~1–2 часа |
| Группа D | ~2–4 часа, самый неопределённый |
| Сборка + тест-матрица | ~1 час на итерацию |
| Перенос аудио | отдельная задача |

**Главный риск** — группа D: `camss.c` и `clk.c`, где мы правим активно
меняющиеся подсистемы. **Главная экономия** — шаг 4: если апстрим уже принял
часть наших правок, патч уменьшится и конфликтов станет меньше.
