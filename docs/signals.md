# Silentype Connector Signal Map

**Status: TEMPLATE — fill from interface card schematic + continuity testing.
Do not wire the carrier board from guesses.**

Printer connects via 26-pin ribbon (2×13 IDC). The Apple /// exposed the same
interface on a DB-9, so several of the 26 pins are grounds/duplicates — the live
signal count is small. Map both.

## 26-pin ribbon (at printer)

| Pin | Signal | Dir (Pico view) | Level | Carrier net | Pico GPIO |
|-----|--------|-----------------|-------|-------------|-----------|
| 1–26 | TBD | | | | |

## Expected signal classes

| Class | Count | Notes |
|---|---|---|
| Thermal head dot drive | N (7?) | **Count unverified.** Confirm whether power FETs are in the printer (logic inputs → Option A) or raw element current is on the connector (Option B). |
| Head element common/return | 0–1 | If on connector, the 0.1 Ω shunt goes here. If internal, shunt is impossible (Option A) — note in BOM.md. |
| Carriage stepper phases | 4 | Unipolar assumed; verify |
| Paper feed | 1–4 | Second motor vs clutch/solenoid |
| Carriage home sensor | 1 | Input |
| Paper-out sensor | 0–1 | May not exist |
| +5 V, VMOT, GND | several | VMOT voltage TBD |

## Level rules

- Pico GPIO is 3.3 V, not 5 V tolerant.
- Outputs → 74AHCT245 (U2 head, U6 motors). Nothing drives a 5 V input directly.
- Inputs (5 V sensors) → 10 k series + 15 k to GND per line.
