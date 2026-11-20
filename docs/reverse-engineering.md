# Phase 0 — Reverse engineering checklist

Phase 0 (documents and firmware sources) is complete. What remains needs the bench and is
tracked in `docs/bring-up.md`. The definitive interface description is
`docs/firmware-archaeology/HARDWARE-INTERFACE.md`.

## 1. Documents

- [x] *Apple Silentype Operation and Reference Manual* — `docs/reference/` (PDF + text).
- [x] Interface card schematics **050-0024-00 / -01** — `docs/firmware-archaeology/apple2-card/`.
- [x] **Printer schematic 050-0023-01** ("Deserializer/Driver Circuit") — `docs/firmware-archaeology/printer/`.
- [x] Apple KB **TA27850 "Silentype: Pinouts"** — `docs/reference/silentype-pinouts-TA27850.txt`.
- [x] Trendcom 100 manual — `docs/reference/trendcom100-manual.pdf` (the Trendcom 200 manual was not found).

## 2. Firmware sources

- [x] **Apple II card ROM 341-0039-00** (2 KB) with Marc Ressl's 2010 commented disassembly
      `silentype.s` — Apple II Documentation Project mirror (under *Interface Cards / Serial*).
      Note: Ressl's `$27`/`$35` labels are swapped and his I/O bit labels are wrong.
- [x] **Apple /// SOS `.SILENTYPE` driver v1.1** — apple3.org, Apple Diskware 681-0009-A;
      disassembled in `docs/firmware-archaeology/BIG.DRIVER.dis`.

| Constant | Value | Source |
|---|---|---|
| Head elements | **7** (Y15 top … Y09 bottom) | /// font table max $7F; schematic J4 |
| Word format | 16 bits, MSB first: head 15–9, R/W̅ 8 = 0, paper 7–4, carriage 3–0 | card ROM `sendState`; schematic |
| Store | XC rising edge with SC low; SC·XC high = clear | card schematic + ROM writes `$1C $18 $1C $0C` |
| Burn tables | 8 levels, main 549–2744 µs (default level 5) | card ROM `defaultVars` |
| Column sequence | step → settle → cold-dot word → all-dot word → off | card ROM `printDotColumn`; /// `$0B7B` |
| Stepper | one-hot wave drive {1,8,4,2}; 4032 µs/step when moving | /// `$0A58`, `$0AC0` |
| Paper feed | ~8 ms/step (///), ~11 ms/step (card ROM) | /// `$0AAE`; card `doFormFeed` |
| Columns per line | 80 characters ≈ 560 columns (ruler check pending) | card ROM `printerRowNum` |
| Paper steps per inch | ≈ 40 | manual p. 21 |
| Home sensor | margin switch to GND, active low | schematic |
| Paper-out sensor | none | schematic |
| Bidirectional trim | −4 µs per column on reverse passes (///) | /// `$0BB4` |

## 3. Still needs the bench

Head element resistance and current, the 555 time-out, the 74LS673 shift direction on a scope,
column count and dot pitch on paper, the best burn level, and the reverse-pass offset. See the
measurement log in `docs/bring-up.md`.

## 4. Exit criteria

`grep TBD_FROM_ROM firmware/src/config.h` returns nothing, and every row of the measurement log
in `docs/bring-up.md` is filled in.
