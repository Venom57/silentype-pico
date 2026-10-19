# Carrier Board — Schematic Description (KiCad capture source)

Capture in KiCad once `docs/signals.md` is verified; don't fab before M1.
`carrier-schematic.svg` and `pinout-connection.svg` are generated from
`gen_schematic.py` / `gen_pinout.py` — edit those, not the SVGs.

## Block diagram

```
 USB ◄──►┌───────────────────────────────────────────────┐
         │ Pico 2 (U1)                                   │
         │  GP2..GP8   HEAD_D1..7 ───► U2 '245 A-side    │
         │  GP9        HEAD_STROBE ──► U5 HC123 trigger  │
         │  GP10..GP13 CARR_PHA..D ──► U6 '245 ──► U3 ULN│
         │  GP14..GP17 FEED_PHA..D ──► U6 '245 ──► U4 ULN│
         │  GP20/GP21  HOME/PAPER  ◄── dividers          │
         │  GP27/ADC1  ISENSE      ◄── R40 shunt + RC    │
         └───────────────────────────────────────────────┘
                    U5 Q̅ ──► U2 OE̅   (R30 10k pullup = disabled)
                    U2 B-side ──► [Option A: J1 head logic]
                                  [Option B: R1-R7 → Q1-Q7 gates → J1]
                    R8-R14 10k pulldowns on every U2 output
```

## Pico 2 pin assignment (mirrors firmware/src/config.h — keep in sync)

| GPIO | Net | Function |
|------|-----|----------|
| GP0/GP1 | UART0 | Debug console 115200 |
| GP2–GP8 | HEAD_D1–D7 | Head dot lines (count = HEAD_DOTS, unverified 7) |
| GP9 | HEAD_STROBE | Rising edge re-arms U5 one-shot |
| GP10–GP13 | CARR_PHA–D | Carriage stepper phases |
| GP14–GP17 | FEED_PHA–D | Paper feed (if fitted) |
| GP20 | HOME_SENSE | Carriage home (divided) |
| GP21 | PAPER_SENSE | Paper-out (if present) |
| GP22 | BTN_FEED | Tactile, to GND, internal pullup |
| GP25 | LED | Status (onboard) |
| GP26 | LED_ERR | Error LED |
| GP27 | ISENSE (ADC1) | Head return shunt (Option B) |

## Net rules

- **U5 runs from the Pico's 3V3** (HC123 works 2–6 V). GP9 drives 1B directly
  (VIH at 3.3 V ≈ 2.3 V, fine). 1Q̅ (3.3 V swing) drives U2 OE̅ — AHCT VIH is 2.0 V,
  so it's a valid high. R30 10 k from OE̅ to 3V3. 1A̅ to GND, CLR̅ to 3V3.
  R31 33 k + C10 100 nF ⇒ ~1.5 ms. (Routing the strobe through U2 would deadlock:
  U2 is disabled exactly when the strobe needs to get through.)
- U2 outputs B1–B7: 10 k pulldown each, always populated. Option B adds 100 R
  gate series to Q1–Q7.
- U6 A1–A8 ← GP10–GP17; B1–B8 → U3/U4 inputs. U3/U4 COM → VMOT.
- R40 0.1 Ω between head common return and GND (Option B); junction → R41 1 k
  → GP27 with C11 10 nF to GND.
- Star ground at power entry. C7 at J1 VMOT pins.
- Test points: head ×7, HEAD_STROBE, U2 OE̅, ISENSE.

## Mechanical (v2)

Measure before assuming anything: original logic board outline, mounting points,
ribbon slot. Model the case interior in Fusion 360; DXF the board outline from KiCad.
