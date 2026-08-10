# Touch bringup — rain/fog (FT8006S SPI / Xinli)

Status (2026-08-03): **SPI + driver + FW download work; no finger events yet.**

## Working stack
| Piece | Detail |
|-------|--------|
| Bus | `spi-gpio` bitbang on QUP pins: **gpio0=MISO, gpio1=MOSI, gpio2=SCK, gpio3=CS** (ACTIVE_LOW) |
| Disabled | `&qupv3_id_0`, `&gpi_dma0`, `&spi0` — GENI FIFO fused off; GPI DMA **hard-hangs** SoC |
| Driver | `drivers/input/touchscreen/fts_spi/` (Focaltech V3.2.2) → `fts_spi.ko` |
| FW | `focaltech_ts_fw_xinli.bin` (131072 B), host PRAM download |
| Chip | ID **0x8642**, FW Ver **0x0e**, Vendor **0x5a** |
| Input | `fts_ts` → `/dev/input/event3` |
| IRQ | gpio80, `IRQF_TRIGGER_FALLING \| IRQF_ONESHOT` |
| Reset | gpio86 via `gpio-shared-proxy` |

## Load recipe (Ubuntu live)
```bash
# /data/local/tmp or rain-touch-load
insmod spi-bitbang.ko
insmod gpio-shared-proxy.ko
insmod fts_spi.ko
insmod spi-gpio.ko   # last — binds DT spi-gpio + touch@0
sleep 3
cat /sys/devices/platform/spi/spi_master/spi0/spi0.0/fts_dump_reg
```

Helper: `tools/rain-overlay/usr/local/sbin/rain-touch-load`

## Observed IC state after FW
- Power Mode **0x00** (forced + pmode poll keepalive; was stuck at 0x01)
- ESD count increments (FW alive)
- **INT count stays 0** — IC does not raise touch IRQs
- Param Ver / status **0x00** (Android vendor also has `drwr_support=0` for 8642)
- Panel TE gpio81 toggles (ABL splash)

## Power / bias — blocked
| Rail | Android | Ubuntu |
|------|---------|--------|
| PMIC gpio9 `disp_lcd_bias_en` | func1, driven | **SPMI write -EPERM**; stays low |
| TLMM gpio98 ENP / ts_avdd | output-high | In `khaje_reserved_gpios` — gpiochip hangs; **pinctrl apply also risk of SOUTH hang** |
| TLMM gpio101 ENN | output-high | same |

Do **not** casually: `insmod gpi.ko`, claim gpio98/101 via gpiochip, or enable `drwr_support` for 8642 (DRAM ECC `status:c3` left chip dead).

## Likely remaining gap
Incell FT8006S likely needs either reliable VSP/VSN/AVDD (bias) **or** full MDSS/DSI panel bringup (not simplefb/ABL-only) before sensing generates INT. DRM blank notifier in Android is suspend/resume only — not the root cause of silent INT.

## DT
- `sm6225-xiaomi-fog.dts` — `spi-gpio` + `touch@0`
- `sm6225.dtsi` — `ts_int_default` (80), `ts_reset_default` (86), unused `lcm_enp/enn` states

## Flash
```bash
adb reboot bootloader
fastboot flash boot_b out/ubuntu-dualboot/boot-linux.img
fastboot reboot
```

Fallback images under `out/ubuntu-dualboot/boot-linux.img.bak-*`.
