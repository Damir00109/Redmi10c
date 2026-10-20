# GPIO pin map — TLMM reserved pins sweep (2026-10-18/19)

Reserved list reduced from 63 to 6 pins after hardware sweeps.

## Methodology

- **Android sysfs GPIO** (stock kernel, no reservation): export → read
  direction/value → write out/1/0 → `edge=both` (INTR_CFG write).
  Markers `try N`/`tryw N` persisted between reboots identify killers.
- **Linux /dev/mem** (Ubuntu slot): direct MMIO reads of
  CTL/IO/INTR_CFG/INTR_STATUS at `0x400000 + TILE + 0x1000*N`
  (WEST=0x100000, EAST=0x900000, SOUTH=0x500000, REG_SIZE=0x1000),
  plus quiesce-equivalent writes (clear enable+raw_status, ack status).

## Lethal pins — reserved FOREVER, never unreserve

| Pin | Tile | Evidence |
|-----|------|----------|
| 14  | WEST | Android export → instant reboot (marker `try 14`) |
| 15  | WEST | same (isolated retest) |
| 16  | WEST | same |
| 17  | WEST | same |

**Why they kill:** these are **LPI pins owned by the LPASS audio
island**.  Stock DT declares them as `lpi_i2s3_*` under
`lpi_pinctrl@ac40000` (= `lpass_tlmm` in our dtsi) and `qupv3_se5_spi`;
`qcom,lpi-offset-tbl` maps LPI pin N → TLMM pin N, i.e. same physical
pads, different register view.  When a pin is island-owned
(`egpio_present`/`egpio_enable` CTL bits 11/12), any AP-side TLMM
register access triggers a fabric/XPU fault → instant SoC reset.
Qualcomm's own downstream pinctrl-khaje reserves `14-17` (plus `0-3`,
`82`, `86` on some devices) via the same `reserved_gpios` mechanism.
They remain readable through the LPASS-side controller (a7c0000,
`gpiochip2`).  Do not remove from the reserved list under any
circumstances — read-only access is fatal too.

## Parked — proven safe, no consumer

| Pin | Reason |
|-----|--------|
| 66  | CSI mux OE — R/W fine, parked until CSI-mux driver exists |
| 67  | CSI mux SEL — same |

## Android-claimed pins (export fails = vendor consumer)

31, 56, 70, 71, 83, 100 — real vendor device owners; not probed for
write on Android, but on Linux they now expose fine (read in pin dump,
no crash). Consumers unknown — likely sensors/peripheral enables.

**SOLVED (2026-10-20): these are the NFC lines.** The stock rain overlay
has `nq@2a` (NXP PN80T-class NFC on i2c1-002a) with:

| Pin | Role in `nq@2a` |
|---|---|
| 31 | `qcom,nq-firm` (firmware download) |
| 56 | `qcom,nq-ven` (enable) |
| 70 | `qcom,nq-irq` + `interrupts` |
| 71 | `qcom,nq-clkreq` |

That is why Android reported them as claimed: the vendor NFC driver holds
them. They are not needed by mainline yet (no NFC stack), so they stay
free and unused.

**Where the claim comes from (checked 2026-10-19):** not from the base DT
or the overlays we had scanned at the time — the `nq@2a` node lives in the
rain overlay, which was re-extracted on 2026-10-20 and is now kept in
`docs/stock-dt/rain-overlay.dts`. Earlier notes on this are below.

- base DT `pinctrl@400000` node (the `pins = "gpioNN"` strings),
- the device-specific overlay `dtbo_a` entry 59 (`RAIN KHAJE IDP nopmi`),
- all 61 overlays in the `dtbo_a` partition (scanned for `"gpio31"`,
  `"gpio56"`, `"gpio70"`, `"gpio71"`, `"gpio90"` — zero hits),
- the stock pinctrl-state dump (`docs/stock-pinmap-full-raw.txt`) —
  only 83 and 100 show up there, as the unnamed states `msm_gpio_83`
  and `msm_gpio_100` / `msm_gpio_100_output_high`.

So the Android claimers are vendor kernel drivers requesting raw GPIO
numbers, not pinctrl states. Identifying them needs the vendor kernel
sources or a `debugfs/gpio` dump from a booted Android — both out of
reach from the mainline side. On Linux they are free, readable and
write-safe, so nothing is blocked by this.

## gpio90 — not a stray GPIO, it is a modem line

`ctl=0x207` decodes as `mux=(ctl>>2)&7 = 1`, `oe=1`, `io=1`. Function 1
on this pin is **`mss_lte`** (`PINGROUP(90, EAST, mss_lte, pbs12,
qdss_gpio12, …)`), i.e. the pad is muxed to a modem/LTE subsystem
signal and driven by it — the bootloader leaves it that way and the
mainline pin is `UNCLAIMED`, so nothing re-muxes it.

That explains both the Android "out, val=1" reading and the earlier
confusion about "something actively driven": it is the modem function,
not a GPIO consumer. Do not re-mux it to GPIO or force it low.

## Unreserved and proven working (Android sysfs + Linux /dev/mem)

| Pins | Observed state |
|------|----------------|
| 34,35,53,75,79,91,96 | input, held **high** |
| 90 | `mss_lte` mux + OE, driven high (see below) — do NOT force low |
| 63,72,73,74,76,77,78 | output low |
| 13,32,33,37–52,55,57–62,64,69,89,94,95,97,99 | input low / quiet |
| 99 | ctl=0x801 — function muxed (func in ctl bits 5:2 = 2) |

All of the above survive INTR_CFG and INTR_STATUS writes — the
"write-fatal" hypothesis was disproven; the two bisect crashes on
large subsets were flaky (the full 52-pin set boots fine).

## Notes

- `icfg=0xE2` reset state on all pins: intr disabled, raw_status off.
- `istat=0x0` everywhere — no pending interrupts.
- `gpio-ranges`: the base `sm6225.dtsi` mapped only `0..101` while
  `pinctrl-khaje.c` has **113** PINGROUP entries (0..112); the fog board
  DTS overrides it with `0..121` anyway, so nothing was broken on this
  device, but the dtsi default was aligned to 113 (2026-10-19).
  Pins 113–120 (`SDC1_*`, `SDC2_*`, `UFS_RESET`) are function-only pads
  with no PINGROUP entry.
