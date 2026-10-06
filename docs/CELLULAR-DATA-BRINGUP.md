# Сотовые данные (IPA → модем → QMAP → интернет) — SM6225 / Redmi 10C

Продолжение `IPA-BRINGUP.md`. Здесь — то, что понадобилось, чтобы поверх
работающего IPA получить **реальный мобильный интернет**, и как это устроено.

## Итог

На чистой загрузке, без ручных действий:

```
ipa 5840000.ipa: IPA driver initialized
ipa 5840000.ipa: IPA driver setup completed successfully
ipa 5840000.ipa: received modem starting event
ipa 5840000.ipa: received modem running event
...
mmcli -m 0     state: connected, access tech: lte, operator: Tele2 RU,
               packet service state: attached
ip -br addr    qmapmux1.0@rmnet_ipa0   10.148.245.52/29
ping -I qmapmux1.0 8.8.8.8     3/3, ~50 ms
curl --interface qmapmux1.0 http://example.com/   200
```

## Три независимые проблемы

### 1. Порядок загрузки: IPA обязан быть раньше модема

Модем при старте спрашивает у AP про IPA-клок по SMP2P (`ipa-clock-query`), а
драйвер IPA берёт «прокси»-ссылку питания на событие power-up модема
(`QCOM_SSR_BEFORE_POWERUP` → `ipa_uc_power()`). Если драйвер ещё не загружен:

* SMP2P-прерывание **фронтовое** (`IRQ_TYPE_EDGE_RISING`) — запрос, пришедший до
  регистрации обработчика, теряется навсегда;
* драйвер не видит power-up модема и печатает
  `unexpected init_completed response` (uC прислал INIT_COMPLETED, а ссылку
  питания никто не взял).

В upstream/Android IPA встроен в ядро и стартует на early-boot — до модема. У нас
`ipa.ko` — модуль, и его грузил udev-coldplug из rootfs **на ~25 с**, тогда как
модем стартует из initramfs **на ~8 с**.

**Исправление:** `pivot-init` грузит `ipa.ko` из initramfs и ждёт готовности
драйвера (`IPA driver setup completed successfully`) **до** старта `remoteproc0`.
Модуль из initramfs остаётся загруженным после `switch_root`, поздний udev-modprobe
становится no-op. Прошивка `ipa_fws.*` и сам `ipa.ko` теперь кладутся в initramfs
(`kernel/build.sh`, `postmarketos/scripts/pack-boot.sh`).

Важно: `rmnet_ipa0` **не** является признаком готовности драйвера — он создаётся
только после QMI-рукопожатия *модема* с IPA (`ipa_qmi.c` → `ipa_modem_start`).

### 2. Модем встаёт в режим `shutting-down`

На этой прошивке модем при загрузке оказывается в QMI-режиме `shutting-down`
(а не `online`). В этом режиме приложения SIM не активируются. ModemManager
переводит модем в `online` своим `enable` — и только после этого SIM становится
`ready`, а модем регистрируется в сети. Поэтому в userspace-сервисе есть
`mmcli -m <id> -e` перед подключением.

### 3. Провижининг-сессия UIM (главная причина «SIM не видна»)

Симптом: карта определяется (`Card status: present`, ICCID читается, приложение
в списке), но приложение остаётся в состоянии `detected` и никогда не доходит до
`ready`; сессии нет:

```
Provisioning applications:
    Primary GW:   session doesn't exist
Application state: 'detected'
```

ModemManager из-за этого падает с `GW primary session index unknown`
(`failed: sim-missing`), а NAS остаётся `not-registered`, хотя радио видит соты.

Карта при этом **исправна**: открытие логического канала к USIM-AID проходит
(`--uim-open-logical-channel=2,A0000000871002FF45FF018902011100` → канал 1).
Проблема на стороне модема — отсутствующая/сломанная провижининг-сессия.

**Восстановление** (`/usr/local/sbin/uim-recover`, идемпотентно):

```sh
qmicli -d qrtr://0 --uim-sim-power-off=<slot>
qmicli -d qrtr://0 --uim-sim-power-on=<slot>
qmicli -d qrtr://0 --uim-reset
qmicli -d qrtr://0 --uim-change-provisioning-session=\
  "session-type=primary-gw-provisioning,activate=yes,slot=<slot>,aid=<USIM AID>"
```

Первые два шага обязательны: без power-cycle/UIM-reset смена сессии падает с
`could not power off SIM: Internal`. После этого:

```
Primary GW:   slot '2', application '1'
Application state: 'ready'
PIN1 state: 'disabled'
```

Сессия **сохраняется в NV модема** и переживает перезагрузки (проверено).

## Userspace: сервис `cellular-data`

ModemManager создаёт QMAP-интерфейс (`qmapmuxN.0`) и bearer, но **не применяет**
IP-конфигурацию bearer'а к интерфейсу — интерфейс остаётся `DOWN` без адреса.
Поэтому `ubuntu/cellular/cellular-data.sh`:

1. ждёт модем в ModemManager;
2. включает модем (`mmcli -m <id> -e`) — выводит его из `shutting-down`;
3. подключает bearer с APN из `/etc/cellular-data.conf`
   (`mmcli -m <id> --simple-connect="apn=...,ip-type=ipv4"`);
4. читает из bearer'а `interface/address/prefix/gateway` и применяет:
   `ip link set ... up`, `ip -4 addr replace ...`, `ip route replace default ... metric 2000`;
5. повторяет проверку каждые 30 с и переприменяет конфиг при смене bearer'а.

Метрика 2000 выбрана, чтобы Wi-Fi (1024 у systemd-networkd) оставался основным
маршрутом, а сотовая — резервом. Сервис корректно отслеживает пересоздание
QMAP-интерфейса (`qmapmux0.0` → `qmapmux1.0` при пересоздании bearer'а).

APN по умолчанию — `internet.tele2.ru` (Tele2 RU). Для другой SIM правится
`/etc/cellular-data.conf`.

## Проверка

```sh
mmcli -L; mmcli -m 0 | grep -E "state:|access tech|operator|packet"
qmicli -d qrtr://0 --uim-get-card-status | head -10
ip -br addr | grep -E "rmnet|qmap"
ip route | grep qmap
ping -c3 -I qmapmux1.0 8.8.8.8
curl -sS --interface qmapmux1.0 -o /dev/null -w '%{http_code}\n' http://example.com/
journalctl -u cellular-data -b
```

DNS: `/etc/resolv.conf` содержит `1.1.1.1` и `8.8.8.8`; через сотовую сеть они
работают (проверено, как и DNS оператора `10.221.138.1`/`10.220.138.1`).

## Разбор случая: сеть отвергала PDP-контекст (2026-10-05/06) — ЗАКРЫТО

**Симптомы.** После загрузки Android (слот A, для установки rootfs) мобильные
данные в Linux перестали подниматься: модем `registered` (домашняя Tele2 LTE,
`PS: attached`), SIM `ready`, профиль верный, но любая активация контекста —
`QMI (14) CallFailed`, `verbose call end reason (3,2001): [cm] no-service`.
Позже, с вставленной SIM, **загрузка стала виснуть** на `pivot: IPA ready`
(следующий шаг — старт модема); без SIM грузилось нормально.
В Android на той же SIM интернет работал, то есть железо и сеть исправны.

**Причина.** Испорченный NV модема: Android-сессия переписала раскладку
провижининг-сессий (наша «Primary GW slot 2» превратилась в `Secondary GW`) и
набор профилей (`ims/sos/xcap`), а мои правки через
`mmcli --3gpp-profile-manager-set` (одна упала с «DS profile error» и стёрла
APN профиля 1) добавили невалидный профиль с типом `initial`. С этим NV модем
не поднимал default-bearer (отсюда `no-service`) и подвисал на UIM-init.

**Лечение.** Восстановление EFS из бэкапа `backups/efs-2026-10-04/`
(modemst1/2, fsg, fsc) — состояние на 4 окт, когда data работал.

**Как восстанавливать (важно):** `fastboot` **не даёт** писать `modemst*` —
«Flashing is not allowed for Controlled Partitions». Поэтому EFS откатывается
только через `dd` из Linux: остановить `rmtfs` (чтобы модем не перезаписывал
EFS), записать разделы, `sync`, перезагрузиться. `fsg`/`fsc` после этого
совпадают с бэкапом побайтово — это проверка, что откат применился.

**Побочные эффекты, которые надо помнить:**

* загруз Android снова портит NV модема: профили → Android-раскладка, сессия →
  `Secondary GW`/пропадает. **Порядок восстановления важен** (проверено):
  1) откат EFS из бэкапа (`efs-restore`) → ребут — возвращает профили/attach;
  2) `uim-recover` → `systemctl restart ModemManager` → ребут — возвращает
     сессию. **UIM-сессия в EFS не хранится**, поэтому откат её не возвращает,
     а без неё data всё равно падает (`no-service` остаётся даже с хорошими
     профилями, и наоборот).
* перезапуск модема (SSR) **убивает WLAN DSP**: `ath10k_snoc ... failed to push
  frame: -108`, `wlan0` остаётся, но не передаёт; перепривязка драйвера не
  помогает — нужна перезагрузка;
* после нескольких подвисаний загрузчик помечает слот B как
  `slot-unbootable: yes` и откатывается на Android. Лечится
  `fastboot set_active b` (снимает флаг, ставит retry-count 7);
* `ModemManager` может падать с SIGSEGV на неудачных connect (backoff в
  `cellular-data` уменьшает частоту).

**Итог после отката EFS** (проверено, загрузка с SIM):

```
7.49s  pivot: IPA ready          9.29s received modem starting event
11.28s pivot: rproc loop done    UIM: Primary GW slot '2', app 'ready'
модем: connected / lte / Tele2 RU / packet: attached
сервис: bearer connected -> qmapmux0.0 10.5.65.124/29
ping -I qmapmux0.0 8.8.8.8 -> 3/3 ~66 ms ; curl -> http=200
```

## Файлы

| Файл | Назначение |
|---|---|
| `postmarketos/initramfs/pivot-init` | `ipa_bringup()` — загрузка IPA до модема |
| `postmarketos/scripts/pack-boot.sh` | `ipa.ko` + `ipa_fws.*` в initramfs |
| `kernel/build.sh` | `ipa_fws.*` в rescue-initramfs |
| `ubuntu/cellular/cellular-data.sh` | сервис: enable модема + bearer + IP-конфиг |
| `ubuntu/cellular/cellular-data.service` | systemd unit (Restart=always) |
| `ubuntu/cellular/cellular-data.conf` | APN/IP_TYPE/METRIC/интервалы |
| `ubuntu/cellular/uim-recover.sh` | восстановление провижининг-сессии UIM |
| `ubuntu/build.sh` | установка перечисленного в образ rootfs |
| `backups/efs-2026-10-04/` | дамп `modemst1/2`, `fsg`, `fsc` (страховка EFS) |
