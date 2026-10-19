# Silentype hardware interface — definitive

Sources (all in this directory tree, all from the Apple II Documentation Project
mirror at mirrors.apple2.org.za/retro/):

- **`printer/printer-schematic-050-0023-01.pdf`** — Apple "Deserializer/Driver Circuit", 6/1980.
  The complete electronics inside the printer. The key document.
- **`apple2-card/341-0039-00.bin`** — the Apple II Synch Printer Interface Card ROM, 2 KB.
- **`apple2-card/silentype.s`** — Marc S. Ressl's commented disassembly of that ROM (2010).
- **`apple2-card/card-schematic-050-0024-0{0,1}.pdf`** — interface card schematics.
- **`../reference/silentype-pinouts-TA27850.txt`** — Apple KB article, DB-9 pinout.
- **`../reference/Apple-Silentype-Operation-and-Reference-Manual.pdf`** — 68 pp, with text layer.
- `README.md` here — the Apple /// SOS driver analysis, which got us most of the way first.

## 1. Connector — DB-9, not a 26-pin ribbon

| Pin | Name | Dir (host view) | Printer side |
|---|---|---|---|
| 1 | Shield | — | NC |
| 2 | +5 V | → | 74LS673, 74LS00, ULN inputs, 555 |
| 3 | Logic GND | — | |
| 4 | **SD** Serial Data | → | 74LS673 I/O pin 6 |
| 5 | **MS** Machine Status | ← | **margin switch to GND** (open = high) |
| 6 | +12 V | → | head elements (via ULN2003), motors, L1 30 µH + C5 3300 µF |
| 7 | Power GND | — | |
| 8 | **XC** Store Clock | → | 74LS673 ST CK (pin 5), and 555 trigger via NAND with SC |
| 9 | **SC** Shift Clock | → | 74LS00 inverter → 74LS673 SH CK (pin 2) |

The "26-pin ribbon" in the earlier docs was the ribbon between the Apple II card and
its DB-9 bracket — irrelevant to us. **The carrier board drives a DB-9.** The whole
`docs/signals.md` 26-pin template is obsolete.

Inputs SC/XC have 3 kΩ pull-ups to +5 V and are 74LS inputs; SD goes to a 74LS673 pin.
**All are 5 V TTL.** 3.3 V on a 74LS input is a legal high (VIH 2.0 V), so a direct
Pico connection would *probably* work, but the pull-ups will source current into 3.3 V
GPIO. Use a 74AHCT125/245 level shifter — same as planned.

## 2. Printer electronics (from 050-0023-01)

```
SD ──────────────────────────► 74LS673 I/O        (16-bit shift register w/ output latch)
SC ──► 74LS00 inv ───────────► 74LS673 SH̅ C̅K̅       (shifts on SC rising edge at connector)
XC ──────────────────────────► 74LS673 ST CK       (latch outputs on XC edge)
SC NAND XC ──► 555 (R1 56k, C1 0.1µF ≈ 6 ms) ──► MC14082 AND gates ──► head ULN2003 enable path
                                                   = HARDWARE BURN CEILING, inside the printer

74LS673 Y09..Y15 ─► RN1 1k ─► IC4 ULN2003 ─► 7 head elements (+12 V common, R4/R5 optional)
74LS673 Y00..Y03 ─► RN2 1k ─► IC5 ULN2003 ─► head-drive (carriage) motor, 4 phases, J1
74LS673 Y04..Y07 ─► RN2 1k ─► IC6 ULN2003 ─► paper-drive motor, 4 phases, J2 (R6 6.8k)
Y08 = R/W̅ (74LS673 mode bit) — tied back to the chip, not a mechanism output
MS  ◄── J5 margin switch (SPST to GND)
```

Motors: 6-wire unipolar (orange/brown/yellow/black + two red centre taps per Ressl's J1/J2
colour list and the schematic). ULN2003 COM pins clamp to +12 V.

### 16-bit shift word (bit 0 shifted first, lands at Y00)

| Bits | Meaning |
|---|---|
| 0–3 | Carriage motor phases 0–3 |
| 4–7 | Paper motor phases 0–3 |
| 8 | R/W̅ — write as 0 |
| 9–15 | Head elements 1–7 (Y09 = bottom dot … Y15 = top dot per schematic J4 labels; verify) |

Transaction: 16 × (set SD, pulse SC), then pulse XC. **The outputs don't change until XC.**
Head elements are ON while their latched bit is 1 **and** the 555 window is open.
The 555 is retriggered by the SC·XC NAND, so a burn is: latch dots=1 via XC → wait →
latch dots=0 via XC. The 555 (~6 ms) kills the head if the host dies with dots latched on.

**This means our carrier-board 74HC123 one-shot is redundant.** The printer already has
the same protection with a longer window. Keep ours only if we want a tighter ceiling
(1.5 ms vs 6 ms); a 6 ms stuck burn on a 40-year-old head is still unpleasant. Decision
deferred to M2; BOM keeps U5 as optional.

## 3. Apple II card timing (ROM 341-0039-00, `printDotColumn` in silentype.s)

Column sequence is the same shape as the Apple /// driver: step motor → pre-heat cold
elements → main burn → off, three latches. Delays are 6502 cycle loops at 1.0205 MHz.

Default tables copied to $CF01+ at init (`defaultVars`):

| Level | `varHeadTab` | `varIntensityTab` | pre-heat µs | **main burn µs** | off µs |
|---|---|---|---|---|---|
| 0 | 40 | 250 | 735 | 549 | 1225 |
| 1 | 60 | 220 | 588 | 823 | 1078 |
| 2 | 80 | 196 | 470 | 1098 | 960 |
| 3 | 100 | 160 | 294 | 1372 | 784 |
| 4 | 120 | 140 | 196 | 1646 | 686 |
| **5 (default)** | 140 | 120 | 98 | **1921** | 588 |
| 6 | 160 | 105 | 24 | 2195 | 514 |
| 7 | 200 | 101 | 5 | 2744 | 495 |

pre-heat = (IntensityTab − 100) × 5 cyc; main = HeadTab × 14 cyc; off = IntensityTab × 5 cyc.
Pre-heat applies only to elements off for the previous two columns (same lookback as the ///).

**Discrepancy with the Apple /// driver** (640 µs main at its default): the two hosts
drive the same mechanism with 3× different main-burn times. Possible reasons: different
+12 V supply sag (Apple II PSU vs ///), or the /// "dark" flag path being the normal
one. Resolution: **start at the /// values (shorter), tune up on paper.** The BURN_US
table in config.h spans both.

## 4. Other constants from the card ROM / manual

| Item | Value | Source |
|---|---|---|
| Columns per line | **80 chars** (`printerRowNum = 80`); right margin max 83 | silentype.s, manual |
| Paper steps per inch | **~40** (form feed default 40 steps "about 1 inch") | manual p.21 |
| Line spacing default | 2 steps between 7-dot rows → **9 steps/text line** | manual p.22 |
| Intensity levels | 0–7, default 5 | manual, ROM |
| Direction | bidirectional default; `$CF0F` bit 7 = unidirectional | manual |
| Hires dump width | 280 px (Apple II hires) across the printable width | ROM |
| Home sensor | margin switch, closed at left margin → MS low. **Active LOW.** | schematic |
| Head element order | Y15 top … Y09 bottom (schematic J4 "TOP DOT"/"BOTTOM DOT") | schematic |

Dot pitch: 80 chars × 7 cols (5 glyph + 2 gap, typical) ≈ 560 columns over the printable
width. If the printable width is ~7.5 in that's ~75 col/in horizontally vs 40 steps/in
vertically — **non-square pixels**. Need a ruler on a real print to pin this down (M1);
the host rasterizer must scale accordingly.

## 5. What this retires

- `HEAD_DOTS` ✔ (was already confirmed)
- Burn tables ✔ (real 8-level table)
- `MAX_COLS` ≈ 560 (80 × 7) — pending ruler check
- `FEED_STEPS_PER_BAND` = 7 + line spacing (2) = 9 for text; 7 for contiguous graphics
- `HOME_SENSE_ACTIVE_LOW` = 1 ✔
- `PAPER_SENSE_FITTED` = **0** — there is no paper-out sensor on the schematic
- The entire parallel-GPIO carrier design → **serial: 3 outputs + 1 input + power**
- Decision 1 (drivers) ✔ in printer. Decision 2 (motors) ✔ two.
