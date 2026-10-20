# patches/ — upstream-серии

`kernel.patch` — единый дифф поверх Linux 7.1.5. Для отправки наверх его нужно
резать на независимые серии; это делает `tools/split-series.sh`:

```sh
./tools/split-series.sh          # -> patches/*.patch
```

Скрипт раскладывает **каждый** файл патча ровно в одну серию и падает, если
что-то не сопоставилось. Порядок серий — от generic-фиксов к платформенным:

| Серия | Что внутри |
|---|---|
| `01-generic-fixes` | ath10k freq==0 guard, clk orphan-reparent crash, `clk-bulk` log level, `q6dsp-lpass-clocks` drvdata ordering, QSEECOM allowlist |
| `02-quiet-boot` | `pr_info`/`pr_err` → `pr_dbg` (partitions, driver core, regulator, q6v5, scsi) |
| `03-camss-sm6225` | CAMSS CSIPHY/CSID/VFE для SM6225 + сенсоры s5kjn1/hi556/ov02b1b/dw9768 |
| `04-pinctrl-khaje` | `pinctrl-khaje`, `pinctrl-msm` (reserved_gpios, quiesce), lpass-lpi |
| `05-ufs-khaje` | UFS host/core quirks + QMP UFS PHY |
| `06-ipa-sm6225` | IPA PAS memory setup |
| `07-clk-interconnect-soc` | GCC/DISPCC/GPUCC SM6375, alpha PLL, interconnect, soc/qcom |
| `08-audio-sm6225` | LPASS макросы, wcd937x, q6asm/q6core, SoundWire, hci_qca |
| `09-display-gpu` | MSM DSI PHY, MDSS, GPU, панель FT8006S |
| `10-usb` | dwc3, qusb2/snps-femto PHY, wusb3801 |
| `11-new-drivers` | написанные с нуля: sh366101, smb1351, aw87xxx, wl2866d, fts_spi, i2c-geni, geni-serial |
| `12-dt-sm6225-fog` | `sm6225.dtsi`, `sm6225-xiaomi-fog.dts` и родственные DTS |

Сгенерированные файлы в git не попадают (см. `.gitignore`) — они всегда
пересобираются из `kernel.patch`, чтобы не разъезжаться с ним.

Описание содержимого каждой серии — в `docs/UPSTREAM-SERIES.md`.
