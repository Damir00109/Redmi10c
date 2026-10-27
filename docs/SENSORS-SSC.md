# Сенсоры (Snapdragon Sensor Core)

Обновлено: 2026-10-27. Акселерометр, датчик света и приближения физически висят
не на i2c AP, а на шине **SSC** внутри ADSP, поэтому kernel-драйверов для них нет
и не будет: протокол закрытый, общаться с датчиками умеет только вендорская
прошивка DSP.

## Цепочка

```
ядро: remoteproc (qcom_q6v5_pas, auto_boot)  ->  adsp.mbn   (один раз за загрузку)
     │
     ├─ fastrpc (built-in)  ->  /dev/fastrpc-adsp
     ├─ qrtr-ns + pd-mapper (in-kernel)
     └─ rmtfs + tqftpserv (сервируют modemst/fsg/fsc и файлы DSP)

userspace: hexagonrpcd  ->  поднимает sensor PD и отдаёт ей виртуальную ФС
           │
           ├─ /persist и /mnt/vendor/persist  ->  {prefix}/sensors/registry/registry
           ├─ /system/vendor/etc/sensors/...  ->  {prefix}/sensors/*
           ├─ /system/vendor/etc/acdbdata     ->  {prefix}/acdb/
           ├─ /sys/devices/soc0               ->  {prefix}/socinfo/
           └─ /usr/lib/qcom/adsp              ->  {prefix}/dsp/adsp

клиент:    libssc / ssccli  ->  значения
           iio-sensor-proxy 3.9 (SSC-бэкенд)  ->  D-Bus net.hadess.SensorProxy
```

## Что чьё

| Слой | Происхождение |
|---|---|
| `adsp.mbn`, реестр SNS на `persist`, ACDB | вендор (Qualcomm/Xiaomi), из стоковых разделов |
| `hexagonrpcd` | **собираем сами** из linux-msm/hexagonrpc (GPL-3.0) |
| `ssccli`, `libssc` | **собираем сами** из codeberg.org/DylanVanAssche/libssc (GPL-3.0) |
| musl-рантайм | **больше не нужен** — оба бинарника теперь glibc |
| `iio-sensor-proxy` | **собираем сами** (3.9 с `-Dssc-support=enabled`) |
| `sensors-setup`, `rain-sensor-{suspend,resume}.sh`, юниты, udev-правила | наши |
| `rain-sensor-bridge.py` | наши, **больше не включён** — заменён на iio-sensor-proxy (оставлен как fallback) |

## Вся userspace-часть собирается из исходников

| Скрипт | Что собирает | Куда кладёт |
|---|---|---|
| `tools/build-hexagonrpcd.sh` | `hexagonrpcd` (linux-msm/hexagonrpc 0.5.0) | `sensors/bin/hexagonrpcd`, `sensors/lib-glibc/libhexagonrpc.so.*` |
| `tools/build-libssc.sh` | `ssccli` + `libssc` (libssc 0.4.4) | `sensors/bin/ssccli`, `sensors/lib-glibc/libssc.so.*` |
| `tools/build-iio-sensor-proxy.sh` | `iio-sensor-proxy` + `monitor-sensor` (3.9, SSC-бэкенд) | `sensors/iio/` (бинарники, юнит, udev-правило, dbus/polkit) |

Зависимости: `meson`, `ninja`, `aarch64-linux-gnu-gcc`, ядерный UAPI
(`out/src/linux/include/uapi/misc/fastrpc.h`) и aarch64-пакеты
`libglib2.0-dev`, `libqmi-glib-dev`, `libprotobuf-c-dev` (для libssc).
В rootfs нужен `libprotobuf-c1` — он добавлен в список пакетов `build-rootfs.sh`.

После обеих сборок `sensors/lib-musl` и musl-загрузчик удаляются: оба бинарника
glibc, а лишний `monitor-sensor` из pmOS-пейлоада не используется.

## hexagonrpcd: почему из исходников

`tools/build-hexagonrpcd.sh` клонирует linux-msm/hexagonrpc в `cache/`,
кросс-собирает под aarch64 (glibc) и кладёт результат в `sensors/bin/` +
`sensors/lib-glibc/`. Требуется `out/src/linux/include/uapi/misc/fastrpc.h`
(то есть сначала собирается ядро).

Почему это лучше pmOS-бинаря:

- **glibc вместо musl** — демону больше не нужен musl-рантайм;
- версия 0.5.0 **сама строит виртуальное дерево** (`construct_root_dir()`):
  `/mnt/vendor/persist`, `/persist`, `/system/vendor`, `/sys/devices/soc0`.
  Проверено на железе: с удалёнными `mnt_persist` и симлинком `/mnt/vendor/persist`
  акселерометр и свет продолжают работать;
- исходники под рукой — можно патчить и апстримить, а не ждать новую сборку.

Каталог прошивки демон умеет угадывать по `/proc/device-tree/{model,compatible}`
(`/usr/share/qcom/{soc}/{vendor}/{codename}`), но у нас model — «Xiaomi Redmi 10C
fog/rain», а каталог `rain`, поэтому `-R` в юните пока передаётся явно.

## Что осталось в `sensors-setup`

После перехода на 0.5.0 от «мусора» осталось только необходимое:

1. смонтировать `persist` (ro) и **скопировать** реестр SNS в `{prefix}/sensors/`
   — копия нужна из-за прав: на `persist` файлы mode 600 uid 1000, а демон
   работает под `fastrpc`;
2. сгенерировать `{prefix}/socinfo/*` (`soc_id`, `revision` из `/sys/devices/soc0`,
   остальное литералами: `hw_platform=bengal`, `platform_subtype=fog`) — наш
   socinfo-драйвер этих полей не отдаёт;
3. записать `END:AP` в `sns_reg_ctrl`;
4. дождаться `/dev/fastrpc-adsp`.

Убрано (было нужно старой сборке): дублирующая копия в `mnt_persist`, симлинк
`/mnt/vendor/persist`, `rm -f /mnt/vendor`.

## Сон и пробуждение

- `rain-sensor-suspend.service` (`Before=sleep.target`) гасит оба `hexagonrpcd`:
  удерживаемая fastrpc-сессия роняет `frpc_dsp` после resume, remoteproc
  перезапускает весь ADSP, и от этого сыпались SoundWire/кодек.
- `rain-sensor-resume.service` поднимает их обратно (иначе сенсоры мертвы до
  ребута). В upstream-юните sensorspd это делается через
  `Conflicts/Before=suspend.target` + `Restart=always` — кандидат на упрощение.

## Десктоп: iio-sensor-proxy вместо своего моста

Ubuntu 26.04 несёт `iio-sensor-proxy` 3.8, а поддержка SSC появилась только в
3.9 (Debian: «Add ssc sensor support», «Enable ssc support on arm64»), поэтому
пакет не подходит и сервис собирается из исходников с `-Dssc-support=enabled`.

Два подводных камня, которые пришлось обойти:

1. **upstream-правило включает для SSC только `ssc-light ssc-compass`.**
   Драйвер акселерометра (`ssc-accel`) в коде есть, но в udev-правиле отключён.
   Наше `79-rain-ssc-accel.rules` добавляет `ssc-accel ssc-proximity`, иначе
   автоповорота не будет.
2. **Матрица ориентации.** iio-sensor-proxy ожидает, что вектор ускорения в
   покое направлен вниз, поэтому `90-accel-mount.rules` теперь инвертирует и Z:
   `-1, 0, 0; 0, -1, 0; 0, 0, -1` (та же матрица, что pmOS использует для
   устройств с нулевой матрицей от прошивки).

Проверено на телефоне: сервис активен, `monitor-sensor` показывает живой свет
(93–95 lux), proximity и tilt `face-up`. `AccelerometerOrientation` при лежащем
аппарате остаётся `undefined` — это штатно: `orientation_calc()` возвращает
предыдущее значение, пока оба угла ниже порога (x≈0, y≈0). Ориентация появится,
когда телефон держат вертикально.

## Известные ограничения

- Прошивка SSC отдаёт **нулевую матрицу ориентации** (`Mount matrix ... all 0`),
  поэтому оси правим снаружи: udev `90-accel-mount.rules` + `ACCEL_MOUNT_MATRIX`.
- `iio-sensor-proxy` в Ubuntu не собирается без libssc — вместо него наш
  `rain-sensor-bridge.py` публикует те же интерфейсы в D-Bus.
- `lsm6dso.so` и прочие DSP-библиотеки датчиков в `{prefix}/dsp/adsp` отсутствуют
  (их нет в стоковом разделе) — журнал ругается, на акселерометр/свет не влияет.
