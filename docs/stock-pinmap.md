# Карта пинов TLMM (SM6225 Khaje) по стоковому DT

Снято с Android-слота: `/proc/device-tree/soc/pinctrl@400000/<group>/<state>/{pins,function}`
скриптом `tools/stock-pinmap.sh`. В стоковом DT пин-стейты задаются двумя способами:
именами (`gpio68`) и числами (u32 BE) — скрипт печатает hex-байты, разбор в `tools/stock-pinmap.sh`-пайплайне.

## Пины, которые у вендора на что-то заявлены (26)

| пин | функция | стейты вендора | у нас |
|---|---|---|---|
| gpio18 | gpio | cam_sensor_rear0_reset_active, cam_sensor_rear0_reset_suspend | свободен |
| gpio19 | SDC1_TB,gpio | cam_sensor_rear1_reset_active, cam_sensor_rear1_reset_suspend, tb_trig1_on | **reserved** |
| gpio20 | cam_mclk | cam_sensor_mclk0_active, cam_sensor_mclk0_suspend | свободен |
| gpio21 | cam_mclk | cam_sensor_mclk1_active, cam_sensor_mclk1_suspend | свободен |
| gpio22 | cci_i2c | cci0_active, cci0_suspend | свободен |
| gpio23 | cci_i2c | cci0_active, cci0_suspend | свободен |
| gpio24 | gpio | cam_sensor_front0_reset_active, cam_sensor_front0_reset_suspend | свободен |
| gpio25 | gpio | pm8008_interrupt | свободен |
| gpio26 | gpio | pm8008_active | свободен |
| gpio27 | cam_mclk | cam_sensor_mclk2_active, cam_sensor_mclk2_suspend | свободен |
| gpio28 | cam_mclk | cam_sensor_mclk3_active, cam_sensor_mclk3_suspend | свободен |
| gpio29 | cci_i2c | cci1_active, cci1_suspend | свободен |
| gpio30 | cci_i2c | cci1_active, cci1_suspend | свободен |
| gpio65 | gpio | cam_sensor_rear2_reset_active, cam_sensor_rear2_reset_suspend, cam_sensor_rear3_reset_active, cam_sensor_rear3_reset_suspend | свободен |
| gpio66 | gpio | cam_sensor_csi_mux_oe_active, cam_sensor_csi_mux_oe_suspend, cam_sensor_rear23_vio_active, cam_sensor_rear23_vio_suspend, cam_sensor_rear2_reset_active, cam_sensor_rear2_reset_suspend | **reserved** |
| gpio67 | gpio | cam_sensor_csi_mux_sel_active, cam_sensor_csi_mux_sel_suspend | **reserved** |
| gpio68 | gpio | cam_flash_torch_active, cam_flash_torch_suspend | **reserved** |
| gpio83 | gpio | msm_gpio_83 | **reserved** |
| gpio84 | gpio | smb1351_int_default | **reserved** |
| gpio85 | gpio | cam_flash_torch_active, cam_flash_torch_suspend | свободен |
| gpio88 | gpio | cd_off, cd_on | **reserved** |
| gpio92 | gpio | wcd937x_reset_active, wcd937x_reset_sleep | свободен |
| gpio93 | gpio | cam_flash_tx_active, cam_flash_tx_suspend | свободен |
| gpio100 | gpio | msm_gpio_100, msm_gpio_100_output_high | **reserved** |
| gpio102 | gpio | cam_sensor_rear0_vaf_active, cam_sensor_rear0_vaf_suspend | свободен |
| gpio104 | gpio | msm_gpio_104, msm_gpio_104_output_high | свободен |

## Reserved-пины, которых в стоковом DT НЕТ (57)

Вендор их ни на что не заявляет — на плате либо не подключены, либо не используются:

`13 14 15 16 17 31 32 33 34 35 36 37 38 39 40 41 42 43 44 45 46 47 48 49 50 51 52 53 55 56 57 58 59 60 61 62 63 64 69 70 71 72 73 74 75 76 77 78 79 89 90 91 94 95 96 97 99`
