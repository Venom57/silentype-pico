# BOM Notes (v0.5 — post schematic discovery)

The printer contains its own 74LS673 latch, three ULN2003 driver arrays, and a 555
burn-time ceiling (schematic 050-0023-01). The carrier therefore reduces to:
**level shifter + pull-up + power + DB-9.** ~$18 all-in.

Both earlier decisions are closed: drivers in printer (Option A, so no FETs, no ULNs,
no shunt), and two motors (both driven by the printer's own ULNs).

## The one open hardware question — U5

The printer's 555 (R1 56 k, C1 0.1 µF ≈ 6 ms) kills the head if the host latches dots
on and dies. Six ms is long for a 1980 head. The optional carrier one-shot (74HC123,
~1.5 ms) would gate **XC** so a stuck host can't even latch for longer than that. But it
also means any legitimate burn >1.5 ms (Apple II card levels 4–7) is impossible.
Decide at M2 after measuring the actual 555 window on the scope; ship without it unless
you intend to run only levels 0–3.

## Power-up discipline

1. +5 V only, printer connected: `send_band.py status` must work; MS reads high/low as
   you push the carriage against the margin switch.
2. +12 V at 200 mA limit: step motors (`send_band.py feed 10`); carriage homes.
3. Raise limit to 2 A; single-column test at intensity 0; scope the 555 window at the
   head ULN input (IC4 pin 1–7) to confirm it's where the schematic says.
4. Only then run text at level 1 and walk up.
