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

## Lethal pins — stay reserved forever

| Pin | Tile | Evidence |
|-----|------|----------|
| 14  | WEST | Android export → instant reboot (marker `try 14`) |
| 15  | WEST | same (isolated retest) |
| 16  | WEST | same |
| 17  | WEST | same |

## Parked — proven safe, no consumer

| Pin | Reason |
|-----|--------|
| 66  | CSI mux OE — R/W fine, parked until CSI-mux driver exists |
| 67  | CSI mux SEL — same |

## Android-claimed pins (export fails = vendor consumer)

31, 56, 70, 71, 83, 100 — real vendor device owners; not probed for
write on Android, but on Linux they now expose fine (read in pin dump,
no crash). Consumers unknown — likely sensors/peripheral enables.

## Unreserved and proven working (Android sysfs + Linux /dev/mem)

| Pins | Observed state |
|------|----------------|
| 34,35,53,75,79,91,96 | input, held **high** |
| 90 | output **high** (ctl=0x207) — something actively driven; do NOT force low |
| 63,72,73,74,76,77,78 | output low |
| 13,32,33,37–52,55,57–62,64,69,89,94,95,97,99 | input low / quiet |
| 99 | ctl=0x801 — function muxed (func in ctl bits 5:2 = 2) |

All of the above survive INTR_CFG and INTR_STATUS writes — the
"write-fatal" hypothesis was disproven; the two bisect crashes on
large subsets were flaky (the full 52-pin set boots fine).

## Notes

- `icfg=0xE2` reset state on all pins: intr disabled, raw_status off.
- `istat=0x0` everywhere — no pending interrupts.
- gpio-ranges in DT maps only 102 lines (`<&tlmm 0 0 102>`); the
  TLMM has 113 groups. Pins ≥102 are outside the gpiochip range.
