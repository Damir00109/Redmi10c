# NFC — NXP SN100U (i2c1-002a, `qcom,nq-nci` на стоке)

## Итог

**Транспорт NFC в mainline работает.** 2026-11-08:

```
~ # echo 0 > /sys/class/rfkill/rfkill0/soft
~ # nfc-nci-test up
NFC_CMD_DEV_UP (CORE_RESET over I2C)...
dev_up: OK — chip answered NCI
```

`NFC_CMD_DEV_UP` прогоняет `nci_open_device()`: драйвер поднимает VEN,
шлёт `CORE_RESET_CMD` по I2C и ждёт `CORE_RESET_RSP` + `CORE_INIT_RSP`.
Успешный `dev_up` = питание, клок, I2C и IRQ в порядке. В `/proc/interrupts`
строка `111: msmgpio 70 Edge nxp-nci_i2c` с ненулевым счётчиком — чип
реально дёргает линию прерывания.

RF-обнаружение (`nfc-nci-test poll` → `NFC_CMD_START_POLL`) запускается без
ошибок; проверка на живой карте ещё не сделана (карты не было под рукой).

**Важная поправка (2026-11-09).** Раньше «START_POLL проходит без ошибок» было
неверно: ядро молча отбрасывало сообщение, потому что `NFC_ATTR_RF_MODE`
объявлен в `net/nfc/netlink.c` как **NLA_U8**, а оба инструмента отправляли его
4 байтами. В dmesg это видно как
`netlink: 'nfc-nci-test': attribute type 11 has an invalid length` (тип 11 —
`NFC_ATTR_RF_MODE`), и никакого RF-обнаружения при этом не запускалось.
Исправлено: поле отправляется одним байтом (`nfc-nci-test.c`, `nfc-read.c`).
Дополнительно `poll` теперь терпит `-EALREADY` от повторного `DEV_UP`
(раньше выходил с `Operation already in progress`) и ждёт 5 минут.

## Что оказалось важно

1. **Чип — SN100U**, не PN80T: стоковая прошивка лежит в
   `/vendor/lib/libsn100u_fw.so`, конфиг `libnfc-nxp.conf` задаёт
   `NXP_SYS_CLK_SRC_SEL=0x02` (PLL) и `NXP_SYS_CLK_FREQ_SEL=0x06` (52 МГц).
2. **Клок даёт PMIC, а не Linux.** В стоковом DT узла `nq@2a` нет ни
   `clocks`, ни регуляторов — только `qcom,nq-irq/ven/firm/clkreq`.
   `pm6125_div_clk1` (9.6 МГц) к NFC не относится. Значит PMIC генерит
   52 МГц после XBL, а CLKREQ (gpio71) только гейтит его — в mainline
   ничего поднимать не нужно, и это подтвердилось.
3. **Драйвер держит VEN в нуле до открытия устройства.** Апстримный
   `nxp-nci-i2c` на probe выставляет `enable-gpios` в 0, поэтому «чип не
   отвечает по I2C» на голой загрузке — нормальное поведение, а не
   отсутствие клока. Ранние выводы в `hardware-inventory.md` были неверны.
4. **rfkill soft-block.** `nfc0` создаётся rfkill-устройством в
   заблокированном состоянии; без `echo 0 > /sys/class/rfkill/rfkill0/soft`
   `dev_up` отдаёт `-ERFKILL`. В обычной системе это делает userspace.
5. `Read failed with error -121` (EREMOTEIO) в dmesg — единичный сбой
   чтения во время инициализации; хендшейк после него проходит.

## DT (mainline)

`sm6225-xiaomi-fog.dts`, под `&i2c2` (= `i2c@4a88000`, QUPv3 SE2):

```dts
nfc@2a {
        compatible = "nxp,nxp-nci-i2c";
        reg = <0x2a>;
        interrupt-parent = <&tlmm>;
        interrupts = <70 IRQ_TYPE_EDGE_RISING>;
        enable-gpios = <&tlmm 56 GPIO_ACTIVE_HIGH>;
        firmware-gpios = <&tlmm 31 GPIO_ACTIVE_HIGH>;
        pinctrl-names = "default";
        pinctrl-0 = <&nfc_int_default>;   /* gpio70, pull-up, input */
};
```

Стоковые состояния pinctrl для сверки: `nfc_int_active` (gpio70,
pull-up), `nfc_enable_active` (gpio56+31, pull-up),
`nfc_clk_req_active` (gpio71, pull-up). Линии VEN/FIRM нельзя ставить в
`pinctrl-0` — их забирает gpiod (конфликт claim'а, как у тачскрина).

## Инструменты

- `tools/nfc-nci-test.c` — netlink-клиент: `up` / `down` / `poll`.
  `poll` шлёт `NFC_CMD_START_POLL` (JEWEL/MIFARE/ISO14443-A/B/FELICA/
  ISO15693, initiator) и печатает `TARGET FOUND idx=… proto=… nfcid1=…`.
- `tools/nfc-probe.c` — прямое управление VEN/FIRM/CLKREQ через gpiolib
  chardev + SMBus-quick по адресу 0x2a. Нужен, когда драйвер не привязан
  (если линии заняты драйвером — деградирует до I2C-пробы).
- `tools/make-slim-rescue.sh` — воспроизводимая сборка «тонкого» rescue
  (13.9 МБ, влезает в `fastboot boot`); кладёт оба инструмента в initramfs.

## Стоковые артефакты (локально, не в git)

`out/nfc-recon/android/`: `libsn100u_fw.so` (прошивка),
`libnfc-nxp.conf`, `libnfc-nci.conf`, `init.fog.nfc.rc`.
Исходник вендорного драйвера: `docs/nfc/stock/nq-nci.c` (+ `.h`) из
`iDeadXD/ai-kernel_xiaomi_fog-msm4.19`.

## Драйвер собирается модулем (для быстрых итераций)

`CONFIG_NFC_NXP_NCI=m` + `CONFIG_NFC_NXP_NCI_I2C=m`: `rebuild-rescue.sh` кладёт
`nxp-nci.ko` / `nxp-nci_i2c.ko` в `/lib/modules` образа. Перезагрузка модуля
(`rmmod` + `insmod`) даёт чипу чистый цикл VEN и новую NCI-сессию — то, чего не
даёт повторный `DEV_DOWN`/`DEV_UP` (после него чип отвечает `-121`/таймаутом).
После `insmod` rfkill-индекс может измениться: NFC — не всегда `rfkill0`,
проверять `cat /sys/class/rfkill/rfkill*/name`.

`nfc-nci-test poll` теперь печатает вердикт ядра (`START_POLL accepted by the
kernel` / `rejected: <errno>`) и поддерживает сужение маски: `poll iso`
(только ISO14443-A), `poll mifare`.

## Контрольный тест в Android (2026-11-10)

Антенна и карта проверены стоком: приложение Mifare Classic Tool (APK с GitHub,
`de.syss.MifareClassicTool`) видит Тройку без каких-либо действий —
`UID: 34 02 16 29 0E 73 56` (7 байт). Значит железо в порядке, и проблема
mainline-стека не в антенне и не в карте.

**Важно:** на хосте не должно быть запущено UltiMaker-Cura — она
автоподключается к `/dev/ttyACM*` и забивает консоль rescue G-кодом `M105`,
после чего ввод в консоль не доходит.

## Дальше

1. Проверить RF-обнаружение на реальной карте (`nfc-nci-test poll`).
2. Userspace-стек: `neard` (netlink + AF_NFC) в Ubuntu rootfs; для
   Type-2 меток нужен разбор `TARGET FOUND` → чтение NDEF.
3. Прошивка чипа (`NFC_CMD_FW_DOWNLOAD`, blob `libsn100u_fw.so`) —
   только если понадобится для карт/платежей.
