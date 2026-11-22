# Bring-up, test plan and measurement log

[README](../README.md) · [Design overview](../docs/design-overview.md) · [Printer interface](../docs/printer-interface.md) · [Firmware design](../docs/firmware-design.md) · [Host software](../docs/host-software.md) · [Carrier board](../hardware/pcb/README.md) · **Bring-up and tests**

Nothing has run on real hardware yet. Verified so far, all in software: column timing and queue
ordering (`firmware/test`), the PIO program (assembled with `pioasm`, cycle-level model of the
PIO, the 74LS673 and Apple's card), bit order and dot orientation (traced from the card ROM and
the Apple /// font), `send_band.py` (unit tests), the PAPPL app's job and device code (`make -C host/pappl-app test`,
against a fake Pico on a pseudo-terminal) and the PCB (KiCad DRC, 0 errors). The PAPPL app also
ran under a real PAPPL 1.4.12 server: printer added, a PNG printed end to end to a fake Pico.

## Equipment

12 V bench supply with an adjustable current limit (≥ 2 A) · oscilloscope (≥ 2 channels,
≥ 20 MHz) · optional logic analyser (save captures to `docs/captures/`) · multimeter · host
with Python 3 + pyserial · 3.3 V USB-serial adapter for J3 · 8.5 in thermal paper.

## Procedure

Add one layer at a time with the supply current-limited; do not continue until the step passes.

1. **Bare board.** No shorts between +12 V, +5 V and GND (TP5, TP6, TP7). C1–C3 match the bold
   "+" on the silkscreen; D1–D3 match the "K" marks.
2. **Board alone** (no Pico, no printer), 12 V at 100 mA. *Pass:* TP6 = 5.0 V ± 0.25 V, power
   LED on, supply < 30 mA, TP2 (SC) and TP3 (XC) ≈ 5 V (bus released), TP1 (SD) ≈ 0 V.
3. **Add the Pico** (current firmware: older builds do not drive GP6), still no printer.
   *Pass:* `send_band.py status` answers (error 3, home time-out, is expected: no margin
   switch). GP6 goes high after boot. On `send_band.py reset`: 16 SC pulses per word, the store
   with SC low, idle SC high / XC low, and SC and XC never high together.
4. **Printer at 300 mA.** *Pass:* the carriage homes to the margin switch within 3 s and
   `status` reads idle; `send_band.py feed 10` advances the paper smoothly.
5. **555 watchdog** (test build that latches one dot and never clears it). *Pass:* the head
   driver input drops after ≈ 5–6 ms, and the motor phases drop too.
6. **2 A, first burn.** `send_band.py burn 0 300`, then `send_band.py pattern --intensity 0`.
   *Pass:* faint, even dots; no smell or discolouration; head current per dot recorded.
7. **Walk the intensity up** one level at a time toward Apple's default 5; record the chosen
   level and burn times in `firmware/src/config.h`.
8. **Images, then the printer app.** `send_band.py image chart.png`; then a page from the PAPPL
   app (`docs/host-software.md`).

## Milestone test plan

| Milestone | Test | Pass criterion |
|---|---|---|
| M1 Bench characterisation | Multimeter on head and motors at the DE-9, printer unpowered | 7 head elements of matched resistance; values logged |
| M2 Carrier built | Procedure steps 1–3 | Documented waveform on the scope; GP6 low clears; MS reads correctly |
| M3 Motion | Step 4; repeated `reset` | Homes 10/10; no stalls across a full sweep |
| M4 First column | Steps 5–6 | Clean dots at a recorded level; 555 time-out measured |
| M5 Test band | `selftest`, `pattern` | Checker even in both directions |
| M6 Protocol end to end | `send_band.py image` | Image recognisable; no NAKs or time-outs over a page |
| M7 Printer app | Add printer; print from the dialog | Appears in the dialog; page prints |
| M8 Command line | `lp -d silentype file.pdf` | Paper matches the PDF |
| M9 Registration | Bidirectional vertical lines; tune `reverse_offset` | Forward/reverse columns within one dot |

## Measurement log

| Measurement | Expected | Measured | Date | Notes |
|---|---|---|---|---|
| Head element count | 7 | | | |
| Head element resistance, each | matched ± 10 % | | | |
| Head current per dot at 12 V | ? | | | sizes F1 and the supply |
| Carriage / paper coil resistance | ? | | | ULN2003 limit 500 mA/ch |
| 12 V current, idle / full burn | < 2 A | | | |
| 5 V printer logic current | ≈ 150 mA | | | F2 is 0.5 A |
| MS at the Pico, open / closed | 3.3 V / 0 V | | | |
| 555 time-out | 5–6 ms | | | |
| Bus word time | ≈ 48 µs | | | |
| Columns per line | ≈ 560 | | | `MAX_COLS` |
| Printable width | ≈ 7.5 in | | | horizontal dpi |
| Paper steps per inch | ≈ 40 | | | vertical dpi |
| Best intensity level | 1–5 | | | `DEFAULT_INTENSITY` |
| Reverse-pass offset | 0 | | | `REVERSE_OFFSET_STEPS` |
