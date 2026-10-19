# Firmware archaeology — Apple /// `.SILENTYPE` SOS driver v1.1

Source: Apple Diskware **681-0009-A** "Silentype /// Driver and Demo", from
apple3.org (`Software/system/drivers/Apple3_Diskware_SilentypeIIIDriver_681-0009-a.dsk`,
SHA-256 `a56f4e16…fafdd90ab`). Volume `SITYPE`, files `BIG.DRIVER` (7680 B) and
`SMALL.DRIVER` (6144 B); both carry the string `.SILENTYPE (C) 1980 Apple` / `Apple 1.1`.
Same mechanism timing in both; SMALL has a smaller font.

The Apple II interface-card C800 ROM has **not** been found. The SOS driver drives the
identical printer mechanism and was written in the same era by the same company, so
its constants are the best available ground truth. Where the Apple /// hardware differs
from the Apple II card (it does — see §2), that's noted.

`BIG.DRIVER.dis` is a linear py65 disassembly, base $0000 = start of driver body
(file offset $0200, after the SOS driver-file comment header). Addresses below use that base.

## 1. Head: **7 elements — CONFIRMED**

The font table at $0DA1–$13A6 (1541 bytes) never sets bit 7 in any glyph column byte;
max value is $7F. Column data is shifted out as one 8-bit byte with bit 7 unused.

`HEAD_DOTS = 7` is no longer a guess.

## 2. Electrical interface — the printer has a shift register

The Apple /// drives the Silentype through its **6522 VIA shift register**, not GPIO
bit-banging:

| Address | What | Used for |
|---|---|---|
| $FFDA | VIA SR | serial data out |
| $FFDF | VIA ACR | set bit 7 to select SR shift-out mode |
| $FFDD | VIA IFR bit 2 | "shift complete" poll |
| $FFD8/9 | VIA T1 | delay timer (1 MHz → µs) |
| $C05A/$C05B | AN1 off/on | latch/strobe pulse |
| $C05E/$C05F | AN3 off/on | second strobe line |
| $C059, $C0DD, $C0DF | AN0 on, misc | init only |
| $C061 | pushbutton 0 input | **carriage home / position sensor** |

Shift-out routine `$0A21` sends **two bytes per transaction**: `$F1` first, then `$F2`.
From the column-print loop at `$0B7B`:

- **`$F1` = head dot byte** (7 bits used)
- **`$F2` = stepper phase nibble** (bits 0–3), from `$33` (carriage) or `$34` (feed)

So the printer-side board is a 16-bit shift register (two 8-bit stages) with latched
outputs feeding the head drivers and the motor drivers. Transitions on AN1/AN3 after the
shift (`$0A45–$0A50`: AN1↑ AN3↑ … AN3↓ … AN1↓) latch and/or enable outputs.

**Implication for the carrier board:** the printer contains the latch *and* the power
drivers (there is no way a 16-bit serial word sinks head/motor current on its own).
This is **Option A** — and the signal set to J1 is just **DATA, CLOCK, two STROBES, one
SENSE, power**. Pin mapping still needs continuity work, but the carrier is much simpler
than drafted: no FETs, no ULNs, no shunt. See §6.

## 3. Column print sequence (`$0B7B`) — three shift-outs per column

Per column, with dot byte `D` (`$FB`) and the previous two columns' bytes `P1`, `P2`:

| Step | Head byte | Delay (T1) | Purpose |
|---|---|---|---|
| step motor | — | — | one phase advance, dir from `$38` bit 7 |
| word 1 | `D & ~(P1 \| P2)` | **512 µs** (`$69/$6A`) | **pre-heat** only elements cold for 2 columns |
| word 2 | `D` | **640 µs** (`$67/$68`), +256 µs if `$35` flag | main burn, all elements |
| word 3 | `0` | **2624 µs** (`$65/$66`) **+ travel adder** | head off / cool |
| tail | — | 512 µs | settle |

Travel adder: `$3B/$3C` is a 16-bit value reset to 0 at the left margin and **decremented
by 4 µs per printed column** during reverse passes (`$0BB4–$0BC3`), added to the word-3
delay unless negative. Net: the column period shrinks slightly across a reverse pass.
This is a bidirectional registration/thermal trim, not something to copy blindly — treat
as a tunable (we have SET_PARAM for it).

**So the original firmware does thermal-history compensation (two-column lookback) and
bidirectional printing.** Both are now in `print_engine.c` on solid footing, but the
compensation form is different from my accumulator — see §7.

Column period ≈ 512 + 640 + 2624 + 512 ≈ **4.3 ms** → ~230 columns/s.

## 4. Stepper drive

`$0A58` (forward) / `$0A6C` (reverse) rotate a 4-bit ring in `$33`:
`LSR; BCC +2; ORA #$08` — one-hot **wave drive**, sequence 0001→1000→0100→0010 (or
reverse). Init writes `$03` to both `$33` and `$34` (`$0CFC`), i.e. two phases on at
start — probably a holding pattern. Paper feed uses the same scheme on `$34` via
`$0A7A`/`$0A8B`, so **there IS a second motor (or at least a second 4-phase coil set)
for paper feed** — `FEED_MOTOR_FITTED = 1`.

Standalone step delay: `$0AC0` loads T1 with `$0FC0` = **4032 µs** per step when moving
without printing (homing, feed). Feed routine `$0AAE` does one step + two 4 ms delays.

## 5. Homing / init (`$0ADA`)

Up to `$16` × `$24` = 792 reverse steps looking for `$C061` bit 7 to go **high**
(sensor active = high on the Apple /// input). Then confirms with debounced steps forward
and back, tolerating 16 steps of search. Timeout → error `$27`.
`HOME_SENSE_ACTIVE_LOW` depends on what sits between the sensor and the pushbutton input
on the ///; bench-verify, but expect active-high at the sensor.

## 6. What this changes in the hardware design

| Was (v0.3) | Now |
|---|---|
| Decision 1 open (drivers in printer vs carrier) | **Option A** — printer has latch + drivers |
| 7 head lines + 4+4 motor lines to J1 | **~5 logic signals**: SDATA, SCLK, STROBE_A (AN1), STROBE_B (AN3), SENSE_IN |
| U2/U6 '245 level shift ×2, FETs, ULNs, shunt | One '245 (or none if the printer's inputs accept 3.3 V — measure) |
| U5 one-shot gating 7 dot lines | One-shot gates the **STROBE/enable** line instead — same protection, one channel |
| Pico GPIO: 16 outputs | 4 outputs + 1 input; Pico **PIO** or SPI peripheral can do the shift natively |

The Apple II card almost certainly does the same thing with a 74LS164/74LS299-class
shifter on the card side feeding the ribbon — that's what Brouhaha meant by the card
being mostly "a pair of 2112 SRAMs" plus glue. **Wire order on the 26-pin ribbon and
which strobe does what still requires the card schematic or continuity + a scope.**

## 7. Firmware changes this drives

- `HEAD_DOTS 7` — confirmed, drop the TBD.
- Burn table: single real value **640 µs** (+256 µs "dark" flag) with a **512 µs
  pre-heat for cold elements**. Intensity 1..5 should scale the 640 (and the Apple II
  manual's POKE-based intensity control likely did exactly that). Replace my invented table.
- Replace heat accumulator with the ROM's 2-column lookback pre-heat (simpler, proven).
- Inter-column off time 2624 µs — much longer than my 200 µs placeholder.
- Step interval while moving: 4032 µs; while printing it's the column period.
- Wave-drive phase table `{1,8,4,2}` (direction-dependent order).
- Transport layer: replace 7+4+4 GPIO writes with a 16-bit shift (PIO).

## 8. Still unknown

- Which physical ribbon pins carry SDATA/SCLK/strobes/sense — **M1**
- Logic polarity at the printer connector — M1
- Columns per line (the driver derives it from a SOS config byte `$5C = 12`?? and
  page tables at `$0D92+`; not yet decoded)
- Dot pitch / feed pitch in physical units — need a ruler and a test print
- Whether the printer's shift-register inputs are 5 V-only
