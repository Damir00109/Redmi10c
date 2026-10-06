# IPA (IP Accelerator) — готовые правки DT для SM6225 / Redmi 10C

Данные взяты из downstream-эталона: `kernel/docs/khaje-downstream-reference.dtsi`
(репозиторий Nikita-Projects/android_kernel_xiaomi_sm6225-devicetrees,
ветка lineage-22.1-topaz, файл `qcom/khaje.dtsi`).

## Ключевые факты

| Параметр | Значение | Источник |
|---|---|---|
| Версия IPA | **v4.2** (`qcom,ipa-hw-ver = <16>`), `IPA_VERSION` читается как `0x40030300` | khaje.dtsi + проверено на железе |
| Поддержка mainline | **есть** — `qcom,sm6115-ipa` → `ipa_data_v4_2` | `drivers/net/ipa/ipa_main.c` |
| База регистров | **`0x5840000`** = wrapper `0x5800000` **+ 0x40000** (`ipahal_get_reg_base()` в вендорном драйвере; та же конвенция, что в mainline `sm6350.dtsi`: `0x01e40000`) | проверено на железе |
| uC shared SRAM | **`0x5847000`** (`IPA_SW_AREA_RAM_DIRECT_ACCESS` @ +0x7000 для v4.2) | reg-карта v4.2 |
| GSI | `0x5804000` (`0x28000`) | khaje.dtsi |
| IRQ | GIC_SPI **257** (ipa), GIC_SPI **259** (gsi) | khaje.dtsi |
| Часы | `RPM_SMD_IPA_CLK` (=68, «core») | rpmcc bindings |
| Прошивка | грузит **AP** (`qcom,gsi-loader = "self"`), `ipa_fws.mdt` из `/lib/firmware` | проверено на железе |
| Порядок | IPA **до** модема (иначе теряется SMP2P `ipa-clock-query`) — см. `CELLULAR-DATA-BRINGUP.md` | проверено на железе |
| icc ID | MASTER_IPA=9, SLAVE_EBI_CH0=6, SLAVE_OCIMEM=16, MASTER_AMPSS_M0=0, SLAVE_IPA_CFG=22 | `dt-bindings/interconnect/qcom,sm6115.h` |

> **История отладки:** все ранние зависания фабрики/нагрев были из-за того, что DT
> мапил wrapper-базу `0x5800000` вместо регистрового окна `0x5840000`: записи уходили
> в дыры wrapper-окна и стопорили NOC. Чтение `0x5800000` возвращало `0x3` (ответ
> wrapper'а) и выглядело «живым». Регистровое окно — только `+0x40000`.

## Правки `sm6225.dtsi`

1. Include:
```dts
#include <dt-bindings/interconnect/qcom,sm6115.h>
```

2. В `smp2p-mpss` (после `smp2p_wlan_1_in`):
```dts
		smp2p_ipa_1_out: qcom,smp2p-ipa-1-out {
			qcom,entry-name = "ipa";
			#qcom,smem-state-cells = <1>;
		};

		/* ipa - inbound entry from mss */
		smp2p_ipa_1_in: qcom,smp2p-ipa-1-in {
			qcom,entry-name = "ipa";
			interrupt-controller;
			#interrupt-cells = <2>;
		};
```

3. В `/soc` — **после** свойств (`compatible = "simple-bus";`), т.к. «Properties must precede subnodes»:

```dts
		system_noc: interconnect@1880000 {
			compatible = "qcom,sm6115-snoc";
			reg = <0x01880000 0x5f080>;
			clocks = <&gcc GCC_SYS_NOC_CPUSS_AHB_CLK>,
				 <&gcc GCC_SYS_NOC_UFS_PHY_AXI_CLK>,
				 <&gcc GCC_SYS_NOC_USB3_PRIM_AXI_CLK>,
				 <&rpmcc RPM_SMD_IPA_CLK>;
			clock-names = "cpu_axi", "ufs_axi", "usb_axi", "ipa";
			#interconnect-cells = <2>;

			clk_virt: interconnect-clk {
				compatible = "qcom,sm6115-clk-virt";
				#interconnect-cells = <2>;
			};

			mmrt_virt: interconnect-mmrt {
				compatible = "qcom,sm6115-mmrt-virt";
				#interconnect-cells = <2>;
			};

			mmnrt_virt: interconnect-mmnrt {
				compatible = "qcom,sm6115-mmnrt-virt";
				#interconnect-cells = <2>;
			};
		};

		config_noc: interconnect@1900000 {
			compatible = "qcom,sm6115-cnoc";
			reg = <0x01900000 0x6200>;
			clocks = <&gcc GCC_CFG_NOC_USB3_PRIM_AXI_CLK>;
			clock-names = "usb_axi";
			#interconnect-cells = <2>;
		};

		bimc: interconnect@4480000 {
			compatible = "qcom,sm6115-bimc";
			reg = <0x04480000 0x80000>;
			#interconnect-cells = <2>;
		};

		ipa: ipa@5800000 {
			compatible = "qcom,sm6115-ipa";
			reg = <0x05800000 0x7000>,
			      <0x05807000 0x2000>,
			      <0x05804000 0x28000>;
			reg-names = "ipa-reg", "ipa-shared", "gsi";
			interrupts-extended = <&intc GIC_SPI 257 IRQ_TYPE_LEVEL_HIGH>,
					      <&intc GIC_SPI 259 IRQ_TYPE_LEVEL_HIGH>,
					      <&smp2p_ipa_1_in 0 IRQ_TYPE_EDGE_RISING>,
					      <&smp2p_ipa_1_in 1 IRQ_TYPE_EDGE_RISING>;
			interrupt-names = "ipa", "gsi",
					  "ipa-clock-query", "ipa-setup-ready";
			clocks = <&rpmcc RPM_SMD_IPA_CLK>;
			clock-names = "core";
			interconnects = <&system_noc MASTER_IPA 0 &bimc SLAVE_EBI_CH0 0>,
					<&system_noc MASTER_IPA 0 &system_noc SLAVE_OCIMEM 0>,
					<&bimc MASTER_AMPSS_M0 0 &config_noc SLAVE_IPA_CFG 0>;
			interconnect-names = "memory", "imem", "config";
			qcom,smem-states = <&smp2p_ipa_1_out 0>,
					   <&smp2p_ipa_1_out 1>;
			qcom,smem-state-names = "ipa-clock-enabled-valid",
						"ipa-clock-enabled";
			qcom,gsi-loader = "modem";
			memory-region = <&ipa_fw_region>;
			status = "okay";
		};
```

4. Патч драйвера (`ipa_main.c`, уже в kernel.patch):
```c
	{
		.compatible	= "qcom,sm6115-ipa",
		.data		= &ipa_data_v4_2,
	},
```

5. Конфиг: `CONFIG_QCOM_IPA=m` (модуль — быстрее итерировать), `CONFIG_INTERCONNECT_QCOM_SM6115=y`.

## Что уже проверено

- Узел виден в DT, драйвер **доходит до probe** ✓
- Ошибка «DT error: ipa-shared memory property» → лечится третьим регионом ✓
- **Осторожно: hard LOCKUP.** Анализ: probe падает (например, на памяти) → драйвер освобождает
  smp2p-IRQ → модем при старте дёргает smp2p-запись «ipa» → обработчика нет → IRQ-шторм →
  LOCKUP на всех ядрах (наблюдалось через +0.2 с после «remoteproc0: powering up modem»).
  **Вывод:** пока probe не проходит, smp2p-IRQ (`ipa-clock-query`/`ipa-setup-ready`) держать в DT
  нельзя. Отлаживать либо без них, либо не поднимая модем в initramfs.

## Рабочий процесс для быстрых итераций

| Что меняем | Цикл |
|---|---|
| Только DTS | `make dtbs` (30 с) → `scripts/repack-dtb.sh` (1 с) → `fastboot flash boot_a` (2 с) → ребут (~60 с) ≈ **1.5 мин** |
| Код драйвера (модуль) | собрать `.ko` → отдать по HTTP (`/tmp/share`) → `insmod` ≈ **1 мин** |

**Не работает:** dtbo-overlay через раздел `dtbo` — ABL его не накладывает для нашего ядра
(проверено маркером: телефон грузится, но свойства в `/proc/device-tree` нет).
Configfs-overlay (`/sys/kernel/config/device-tree/overlays`) в этом ядре отсутствует.

## РЕЗУЛЬТАТ БИСЕКЦИИ (2026-09-29, автономно)

Проверено на живом устройстве (тестовый образ → слот B, A оставался стабильным):

| Тест | Состав DT | Результат |
|---|---|---|
| 1 | **только icc-провайдеры** (`system_noc`, `config_noc`, `bimc`) | ❌ **FASTBOOT — краш-петля** |
| 2 | icc + узел IPA без smp2p-IRQ | ❌ краш-петля |
| 3 | icc + узел IPA + smp2p-IRQ | ❌ краш-петля |

**Вывод: вешают сами icc-провайдеры, узел IPA тут ни при чём.**

Вероятная причина: драйвер `icc-rpm` (`drivers/interconnect/qcom/icc-rpm.c:377-416`)
при probe выставляет «bus clock rate» через RPM (`qcom_icc_rpm_set_bus_rate`).
Если RPM-ресурс bus-clock не поддерживается прошивкой PMIC, ответа не приходит —
и это вешает ядро. Смотреть надо `drivers/interconnect/qcom/sm6115.c`
(описатели `bus_clk_desc`) и `icc-rpm-clocks.c`.

**Следующий шаг для IPA:** сначала отдельно разобраться с icc-rpm (без него IPA
не поднимется — драйверу нужны interconnect-пути «memory/imem/config»).

## Про A/B и счётчики retry

Наш Linux **не отмечает успешную загрузку** (нет аналога Android `boot_control` HAL),
поэтому ABL каждый раз уменьшает `slot-retry-count` активного слота. Наблюдалось: слот A
дошёл до 4, слот B до 0 («unbootable»). Лечится `fastboot set_active <slot>` (сбрасывает
счётчик), но правильнее — писать флаг успешной загрузки в BCB (`misc`/`devinfo`).

При краш-петле ABL **не** переключается на другой слот автоматически — уходит в fastboot.
Стабильность даёт то, что второй слот остаётся с рабочим образом (переключение вручную).

## НАХОДКИ ИЗ СБОРКИ LINEAGEOS (2026-09-30)

Проанализирован `lineage-23.2-20260823-UNOFFICIAL-fog.zip` (1.09 ГБ, A/B OTA payload).
Разобрано payload-dumper-go, образы vendor/odm/boot/vendor_boot/dtbo.

### 1. ПРОШИВКА IPA НАЙДЕНА ✓
В `vendor/firmware/`: `ipa_fws.mdt` + `ipa_fws.b00..b04` + `ipa_fws.elf`
(+ дубликаты `scuba_ipa_fws.*`). Скопировано в `kernel/firmware/ipa/`.
Это позволяет уйти от deprecated `modem-init` на штатный режим `qcom,gsi-loader = "self"`
(AP сам грузит GSI-прошивку через PAS id 15).

### 2. Рецепт Android (в `kernel/docs/android-ipa/`)
```
etc/init/ipa_fws.rc:   on early-boot
                           write /dev/ipa 1
```
Плюс `bin/ipacm` + `etc/IPACM_cfg.xml` (IPA Config Manager — настройка data-path в Android).
Для mainline это не нужно: конфигурацию делает сам драйвер IPA.

### 3. SMMU stream IDs (то, чего не хватало в узле IPA) ✓
```
ipa_smmu_ap   iommus = <&apps_smmu 0x140 0x0>   qcom,iommu-dma-addr-pool = <0x10000000 0x30000000>
ipa_smmu_wlan iommus = <&apps_smmu 0x141 0x0>   qcom,iommu-dma = "atomic"
ipa_smmu_uc   iommus = <&apps_smmu 0x142 0x0>   qcom,iommu-dma-addr-pool = <0x40400000 0x1fc00000>
```
В mainline-узле это `iommus = <&apps_smmu 0x140 0x0>, <&apps_smmu 0x142 0x0>;`

### 4. Память IPA — НАШИ значения подтверждены ✓
```
downstream (реальное устройство): ipa_fw_region@56600000, ipa_gsi_region@56610000
у нас в DT:                       ipa_fw_region@56600000, ipa_gsi_region@56610000 ✓✓
```
(значения 0x556/0x561 из публичного khaje.dtsi — от других вариантов устройства)

### 5. Прочее
- IRQ 0x101/0x103 = 257/259 ✓ совпадает, версия `qcom,ipa-hw-ver = 0x10` = 16 = **v4.2** ✓
- Часы: downstream `clock-names = "core_clk"`, mainline ждёт `"core"` (у нас "core" ✓)
- Downstream использует **старый msm_bus**, а не mainline interconnect — поэтому в его DT нет icc-узлов,
  а mainline-драйвер IPA interconnect требует. Это и есть оставшийся блокер (icc-rpm вешает ядро).

### 6. dtbo: BIG-ENDIAN
Все поля `dt_table_header`/`dt_table_entry` в разделе `dtbo` — **big-endian**
(`magic=0xd7b7ab1e`, 61 запись, page 4096). Мой первый маркер был little-endian и ABL его
проигнорировал. BE-маркер ABL **уже обработал** (загрузка сломалась) — значит механизм рабочий,
но формат overlay надо подобрать точнее (тест с `target-path = "/"` не подошёл).
