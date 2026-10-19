# BOM Notes

`BOM.csv` is orderable (~$32 before PCB). Two Phase-0 decisions gate it:

## Decision 1 — Where do the power drivers live?

- **Option A — in the printer.** Connector carries logic-level head/motor signals.
  Populate U2/U5/U6, omit Q1–Q7, R1–R7, R40/R41. **The shunt is impossible here**
  (current never crosses the carrier) — `ERR_HEAD_FAULT` stays unimplemented and
  `head_mA_last` reads 0.
- **Option B — on the carrier.** Raw element current on the connector. Populate FETs,
  shunt, size F1 from measured current.

Either way U2 (head level shifter) and U5 (monostable) are populated: the one-shot
gates U2's OE, which is how the hardware burn ceiling works in both options.

## Decision 2 — One motor or two? U4 covers either; delete if unused.

## The head-enable one-shot (U5) — why and how

Firmware pulses `HEAD_STROBE` (GP9) once per column, *then* raises dot lines, *then*
lowers them. U5's Q̅ drives U2 OE̅: low (enabled) for ~1.5 ms after each strobe
edge, then high (tri-state). R8–R14 pull the tri-stated lines to GND. A Pico hang
with dot lines high therefore ends at 1.5 ms regardless of software. Verify on a
scope in M2 before the head rail is ever connected: strobe once, hold a dot line
high in a test build, confirm the U2 output drops at ~1.5 ms.

Residual: HC123 is retriggerable. A hang *inside a loop that keeps strobing* with
lines high would defeat it. Swap to 74HC221 (non-retriggerable) if that bothers you;
it means the burn time is then hard-capped per strobe and firmware must not strobe
faster than T.

## Power-up discipline

1. First power: bench supply limited to 200 mA, head rail disconnected, motors only.
2. Verify U5/U2 gating on the scope (M2).
3. Head rail connects only after 1–2 pass and `grep TBD_FROM_ROM` is clean.

## v2 (optional)

Respin as a 2-layer KiCad board to fit inside the Silentype case. **Measure** the
original logic board footprint, mounting points, and the ribbon slot — nothing here
assumes where it mounts.
