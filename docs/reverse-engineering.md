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
- [ ] **Apple /// SOS `.SILENTYPE` driver**. The Apple /// had a built-in
      Silentype port; its SOS driver implements the same mechanism control and
      is on Apple /// SOS driver disks (asimov /// archives). Often easier to find.

Disassemble (6502 — `da65`, Ghidra w/ 6502, Virtual ][ debugger) and record:

| Constant | Value found | Address | Notes |
|---|---|---|---|
| Stepper phase table (carriage) | TBD | | full/half step? |
| Step interval + ramp profile | TBD | | µs between steps, accel steps |
| Head burn time per intensity | TBD | | the darkness POKE table |
| Inter-column cooldown | TBD | | |
| Head element count | TBD | | **verify — the "7" everywhere is unconfirmed** |
| Paper feed steps per band | TBD | | ⇒ native vertical DPI |
| Columns per line | TBD | | ⇒ raster width |
| Sensor polling / polarity | TBD | | debounce, timeout |
| Head-enable vs motion sequencing | TBD | | safety-critical |
| Reverse-pass column offset | TBD | | M9 |

## 3. Bench characterization (printer unpowered unless stated)

| Measurement | Expected | Measured | Notes |
|---|---|---|---|
| **Head element count** | 7 (unverified) | | count pins/traces at connector |
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
| Connector at printer | 26-pin 2×13 | | photograph + continuity map |

## 4. Ground truth capture (if a working Apple II + card is available)

- [ ] Logic analyzer on head dots, motor phases, sensors during self-test,
      a text line, a graphics dump, intensity min vs max.
- [ ] Save to `docs/captures/` (sigrok `.sr`). Diff against ROM constants.

## 5. Exit criteria

`grep -r TBD_FROM_ROM firmware/` returns nothing, `HEAD_DOTS` is confirmed,
and `docs/signals.md` is continuity-verified.
