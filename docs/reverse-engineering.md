# Phase 0 — Reverse Engineering Checklist & Measurement Log

Nothing downstream is trustworthy until this file is filled in.

## 1. Documents to acquire

- [ ] *Apple Silentype Operation and Reference Manual* — archive.org. Programming
      info (intensity POKEs, control codes), paper spec, service detail.
- [ ] Silentype **interface card schematic** — Apple II Documentation Project
      mirrors (mirrors.apple2.org.za / asimov). Also Apple Service Technical
      Procedures for the Silentype.
- [ ] Apple KB **TA27850 "Silentype: Pinouts"** via web.archive.org.
- [ ] Trendcom 200 documentation (mechanically identical; Trendcom kept the
      onboard controller, so its docs describe the mechanism directly).

## 2. Firmware sources (timing ground truth)

Two independent sources contain the drive logic. Get at least one:

- [ ] **Apple II Silentype card C800 ROM** (2 KB). Look in Apple II Documentation
      Project ROM archives and asimov. Do **not** assume an emulator ships it —
      AppleWin/MAME are not known to emulate the card.
- [x] **Apple /// SOS `.SILENTYPE` driver** — FOUND: apple3.org, Apple Diskware
      681-0009-A. Disassembled; see `docs/firmware-archaeology/README.md`.

Disassemble (6502 — `da65`, Ghidra w/ 6502, Virtual ][ debugger) and record:

| Constant | Value found | Address | Notes |
|---|---|---|---|
| Stepper phase table (carriage) | one-hot {1,8,4,2} | $0A58 | wave drive |
| Step interval + ramp profile | 4032 µs, no ramp | $0AC0 | column period governs while printing |
| Head burn time per intensity | 640 µs (+256 dark); 512 pre-heat; 2624 off | DIB cfg $65–$6A | one level + dark flag in ///; II POKE table still unknown |
| Inter-column cooldown | 2624 + 512 µs | $65/$66, $69/$6A | |
| Head element count | **7** | font $0DA1+ | max glyph byte $7F |
| Paper feed steps per band | TBD | | ⇒ native vertical DPI |
| Columns per line | TBD | | ⇒ raster width |
| Sensor polling / polarity | $C061 bit7 high = home | $0ADA | 792-step search, 16-step debounce |
| Head-enable vs motion sequencing | step → preheat → burn → off | $0B7B | per column |
| Reverse-pass column offset | −4 µs/column timing trim, not steps | $0BB4 | tunable |

## 3. Bench characterization (printer unpowered unless stated)

| Measurement | Expected | Measured | Notes |
|---|---|---|---|
| **Head element count** | 7 (ROM-confirmed) | | sanity-check at connector |
| Head element resistance, each | matched ±10% | | |
| Dead/weak elements? | none | | |
| Head element return path | ? | | VMOT common inside printer, or on connector? decides shunt placement |
| Stepper type (carriage) | 4-φ unipolar likely | | |
| Stepper coil resistance | | | ⇒ drive current; ULN2003A is 500 mA/ch |
| Second motor for paper feed? | from schematic | | or clutch off carriage motor |
| Supply rails required | +5 V + motor/head rail | | what did the card feed it? |
| Head current, 1 dot / all dots | | | sizes FETs, F1, C7 |
| Sensors present | home, paper-out? | | switch / opto |
| Sensor logic levels | | | |
| Connector at printer | 26-pin 2×13 | | **expect ~5 logic lines: SDATA, SCLK, 2 strobes, SENSE + power/GND** — find them |

## 4. Ground truth capture (if a working Apple II + card is available)

- [ ] Logic analyzer on head dots, motor phases, sensors during self-test,
      a text line, a graphics dump, intensity min vs max.
- [ ] Save to `docs/captures/` (sigrok `.sr`). Diff against ROM constants.

## 5. Exit criteria

`grep -r TBD_FROM_ROM firmware/` returns nothing, `HEAD_DOTS` is confirmed,
and `docs/signals.md` is continuity-verified.
