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
| 8 | **XC** Store Clock | → | 74LS673 ST CK (pin 5, rising edge); also NAND with SC → 74LS673 ST CLR̅ (SC=XC=1 clears all outputs) |
| 9 | **SC** Shift Clock | → | 74LS00 inverter → 74LS673 SH CK̅ (pin 2): shifts on SC **rising** edge |

The "26-pin ribbon" in the earlier docs was the ribbon between the Apple II card and
its DB-9 bracket — irrelevant to us. **The carrier board drives a DB-9.** The whole
`docs/signals.md` 26-pin template is obsolete.

Inputs SC/XC have 3 kΩ pull-ups to +5 V and are 74LS inputs; SD goes to a 74LS673 pin.
**All are 5 V TTL.** 3.3 V on a 74LS input is a legal high (VIH 2.0 V), so a direct
Pico connection would *probably* work, but the pull-ups will source current into 3.3 V
GPIO. Use a 74AHCT125/245 level shifter — same as planned.

## 2. Printer electronics (from 050-0023-01) and the bus protocol

Traced from the 1330-ppi scan inside `printer/printer-schematic-050-0023-01.pdf` (page 2)
and the card schematics `apple2-card/card-schematic-050-0024-0{0,1}.pdf` (identical in this
area). Pin numbers below are schematic pin numbers.

```
SD (J3-4) ───────────────────────────► 74LS673 I/O  (pin 6, serial in; CS̅ pin 1 tied to GND)
SC (J3-9, R3 3k↑) ─► IC2 74LS00 4,5→6 (inverter) ─► 74LS673 SH CK̅ (pin 2)  → shift on SC ↑
XC (J3-8, R2 3k↑) ───────────────────► 74LS673 ST CK  (pin 5)  → latch Y00..Y15 on XC ↑
SC, XC ─► IC2 9,10→8  NAND(SC,XC) ─┐
555 OUT (pin 3) ───────────────────┴─► IC2 12,13→11, 1,2→3  =  AND ─► 74LS673 ST CLR̅ (pin 4)
        ST CLR̅ = NAND(SC,XC) · 555out   → storage register CLEARED (all 16 outputs 0) when
                                           SC=1 AND XC=1, or when the 555 output is low.

Head-on watchdog (the 555 is NOT triggered by SC/XC):
IC4 head ULN2003 outputs 10..16 ─► MC14082B (dual 4-in AND, at +12 V) ─► "all head elements off"
   ─► R6 6.8k ─► spare IC6 ULN2003 channel (in 5 → out 12) ─► 555 TRIG+TH node (R1 56k↑, C1 0.1 µF)
   all elements off → ULN ch. sinks → C1 held low → 555 OUT high → ST CLR̅ released
   any element on   → C1 charges via R1 (τ = 5.6 ms) → at 2/3 Vcc (≈5-6 ms) OUT low → CLEAR
   the clear turns every element off → C1 dumped through the ULN → OUT high again (self-reset)

74LS673 Y09..Y15 ─► IC4 ULN2003 ─► 7 head elements (+12 V common, R4/R5 optional); Y15 = J4-8 TOP DOT
74LS673 Y00..Y03 ─► IC5 ULN2003 ─► head-drive (carriage) motor, 4 phases, J1
74LS673 Y04..Y07 ─► IC6 ULN2003 ─► paper-drive motor, 4 phases, J2
RN1/RN2 1k = pull-ups to +5 V on the Y lines (LS outputs → ULN inputs)
Y08 (pin 16) ─► 74LS673 R/W̅ (pin 3) — the chip's own mode bit; must always be latched 0
MS  ◄── J5 margin switch (SPST to GND)
```

Motors: 6-wire unipolar (orange/brown/yellow/black + two red centre taps per Ressl's J1/J2
colour list and the schematic). ULN2003 COM pins clamp to +12 V.

**Consequences:**

- **SC=1 and XC=1 together is a "clear" command** — all 16 latch outputs go to 0 (head off,
  both motors de-energised, Y08=0 so the 673 is back in write mode). It is asynchronous and
  level-sensitive. Apple's `resetPrinter` (writes `$00`) and the card's power-on reset both put
  the bus in exactly this state. A store must therefore be done with **SC low**.
- **The 555 does not gate the head path and is not retriggered by stores.** It is a watchdog on
  continuous head-on time: if any element stays latched on for ≈5-6 ms (R1·C1·ln3 = 6.2 ms from
  0 V, ≈5.3 ms from the ULN's ≈0.7 V saturation; Ressl quotes 5 ms) it clears the **whole**
  storage register — motors included. It re-arms itself as soon as all elements are off.
  Pre-heat + main burn must stay well under ≈5 ms (the ROM's worst case is 0.7 + 2.7 ms), and
  every burn must end with an explicit head-off word (the ROM's third `sendState`).
- Our carrier-board 74HC123 one-shot remains redundant with this watchdog; it would only add
  a tighter ceiling (1.5 ms vs ≈5-6 ms). Decision still deferred to M2; BOM keeps U5 optional.

### Apple II card bus decode (050-0024-01; ROM writes to `$C081+slot·16`)

The card latches the written byte into a 74LS174 (B4). **Ressl's "Logic Interface" bit
labels (bit 2 = clock, bit 3 = data, bit 4 = store) are wrong**; the schematic gives:

| Data bit | 74LS174 | Drives | Effect |
|---|---|---|---|
| D0 | Q0 | 74LS126 A8 in | SD level |
| D3 | Q3 | 74LS126 A8 OE (active high) | 1 = drive SD; 0 = tri-state, R5 1.1k pulls SD low |
| D2 | Q2 | 74LS04 → XC | **XC = NOT D2** |
| D4 | Q4 | 74LS02 NOR → SC | 1 forces SC low |
| D6 | Q1 | 74LS125 A3 OE̅ (SC driver) | 0 = drive SC; 1 = tri-state (printer's 3k pulls SC high) |
| D5 | Q5 | 74LS04 → ROM EN̅ | not a printer signal |
| D1 | — | 74LS08 → 74LS74 B8 D, clocked by Φ1 | 1 = one ≈1 µs SC **low** pulse after the write |

SC = NOR(B8.Q, Q4) when driven. Decoding the ROM's writes:

| ROM write | SD | SC | XC | Meaning |
|---|---|---|---|---|
| `$0E \| bit` (×16) | bit | 1 → **0 for ≈1 µs → 1** | 0 | present bit, then SC rising edge shifts it |
| `$1C` | 0 | **0** (Q4) | 0 | take SC low *before* the store |
| `$18` | 0 | 0 | **1** | XC rising edge with SC low → **store**, no clear |
| `$1C` | 0 | 0 | 0 | XC low |
| `$0C` | 0 | **1** | 0 | SC back high (a 17th shift edge; only the shift register sees it) → idle |
| `$00` (`resetPrinter`) | 0 (R5) | 1 | 1 | SC·XC → **clear all outputs** |

Bus idle between `sendState` calls is therefore **SC=1, XC=0, SD=0**. Measured from the ROM
cycle counts: SD settles ≈1.5 µs before the SC rising edge, SC is low ≈1 µs, a bit every
≈19 µs; store phases are 7 CPU cycles (≈7 µs) apart. Ressl's header formula
"~STRCLK = !(RSC & SC) & 555 Timer" is really ST **CLR̅** (pin 4) and matches the schematic;
"timer triggered through the logical AND of the printer pin outputs" is the MC14082 path.

### Ressl's zero-page labels are swapped

In `silentype.s`, `stateStepper = $27` actually holds the **head dot byte** (`printDotColumn`
stores `varStepper1/2` — dot column / cold dots — there), and `statePin = $35` holds the
**motor phase byte** (`varStatePin`, rotated as a 4-bit ring with `LSR`/`ORA #$08`; paper
phases in the high nibble via `ASL`×4). `sendState` does `ASL $27`, then 16 × (`ASL $35`;
`ROL $27`; `ROL A`; `STA`), so the transmit order first → last is
**head bit 6, 5, …, 0, then 0 (R/W̅), then motor bits 7 … 0**. The Apple /// SOS driver also
sends the head byte first.

### 16-bit shift word — bit 15 shifted first, first-in lands at Y15

The 74LS673 shifts serial-in toward Q15, so after 16 clocks the first bit is at Y15. Given the
ROM order above and the schematic's Y-to-driver wiring, this is the only orientation in which
the Apple card could have worked (the reverse would put head data on the motor phases).

| Word bit = YN | Meaning |
|---|---|
| 15–9 | Head elements: Y15 = top dot (J4-8) … Y09 = bottom dot (J4-2); `sr_make` head bit i → Y(9+i) |
| 8 | Y08 = 74LS673 R/W̅ — **must be 0** (a 1 puts the chip in read mode: its I/O pin becomes an output fighting SD; only a clear recovers) |
| 7–4 | Paper motor phases (Y07..Y04) |
| 3–0 | Carriage motor phases (Y03..Y00) |

### What the Pico firmware sends (`silentype_sr.pio`, `shiftreg.c`)

All of SD/SC/XC are generated by one PIO state machine (OUT = SD/GP2, 2-pin side-set =
SC/GP3 + XC/GP4), one complete transaction per `sr_write()`, IRQ 0 after the store:

```
idle  SC=1 XC=0 SD=0
16×   SD=bit (word bit 15 first) while SC=1 ─1.0 µs─ SC=0 ─1.0 µs─ SC=1 (shift) ─0.5 µs─
      → SD set-up 2.0 µs before the shift edge, hold 0.5 µs, SC low 1.0 µs, 2.5 µs/bit
store SD=0 SC=0 ─2 µs─ XC=1 (latch) ─2 µs─ XC=0 ─2 µs─ SC=1 (idle) + IRQ
      ≈ 47.5 µs per word at a 2 MHz PIO clock; latch edge ≈ 43 µs after sr_write() starts
```

`sr_init()` first holds SC=XC=1 for ≈10 µs (clearing whatever the latch powered up with,
including Y08), then idles and latches an all-zero word. `sr_write()` also masks bit 8.

### Verified vs inferred — scope at M2

| Item | Status |
|---|---|
| SC → inverter → SH CK̅; XC → ST CK; NAND(SC,XC)·555 → ST CLR̅ | read from schematic |
| 555 fed from MC14082 AND of head ULN outputs via R6 + spare ULN channel | read from schematic |
| Card decode (table above), ROM write sequence and idle state | read from card schematic + ROM |
| Y15 → IC4 in 1 → out 16 → J4-8 TOP DOT … Y09 → J4-2 BOTTOM DOT; RN1/RN2 are pull-ups to +5 V | read from schematic |
| Bit order (first-in → Y15) | inferred from ROM order + schematic wiring (+ /// driver); not bench-tested |
| 74LS673 edge polarities (shift on SH CK̅ falling, store on ST CK rising, async clear) | inferred from schematic bubbles; datasheet not in repo |
| 555 ceiling ≈5-6 ms; clears motors as well as head | computed from R1/C1; **measure** |
| ns-level SC/XC skew through the 74AHCT125 at the store/clear | not modelled |

At M2, with a scope on DB-9 pins 4/9/8 and a logic probe on 74LS673 pins 4 (ST CLR̅) and 5:
confirm ST CLR̅ never pulses during `sr_write()`; confirm one ST CK rising edge per word with SC
low; latch a single head bit and a single carriage phase and confirm which ULN output pulls low;
hold one dot on and time the 555 clear (and confirm the motors drop too).

**Carrier-board note:** while the Pico is in reset its pins float. Pull SC and XC up (to 3V3
on the 74AHCT125 inputs) and SD down, so an unprogrammed / resetting Pico presents SC=XC=1 =
"clear" to the printer, like the Apple card does on reset.

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
