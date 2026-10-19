# Silentype Connector Signal Map — DB-9 (DEFINITIVE)

Source: Apple KB TA27850 + printer schematic 050-0023-01 (`docs/firmware-archaeology/`).
The earlier 26-pin template was wrong — that ribbon is internal to the Apple II card.

| DB-9 pin | Name | Dir (Pico view) | Printer side | Carrier net | Pico GPIO |
|---|---|---|---|---|---|
| 1 | Shield | — | NC | chassis | — |
| 2 | +5 V | → | 74LS673/74LS00/ULN logic/555 | +5V rail | — |
| 3 | Logic GND | — | | GND | — |
| 4 | SD — Serial Data | → | 74LS673 I/O (pin 6) | SD_5V | GP2 via U2 |
| 5 | MS — Machine Status | ← | margin switch to GND; open = high | MS (10k pull-up to 3V3) | GP5 |
| 6 | +12 V | → | head (via ULN2003), both motors | VMOT = **+12 V** | — |
| 7 | Power GND | — | | GND (star) | — |
| 8 | XC — Store Clock | → | 74LS673 ST CK; NAND w/ SC → 555 | XC_5V | GP4 via U2 |
| 9 | SC — Shift Clock | → | 74LS00 inverter → 74LS673 SH̅ C̅K̅ | SC_5V | GP3 via U2 |

Levels: SC/XC have 3 kΩ pull-ups to +5 V inside the printer and are 74LS inputs. Drive
them through a 74AHCT125 (3 channels) or '245. MS is an open switch to ground — pull up
on our side to 3V3; **do not** let +5 V onto a Pico pin.

Shift word: bit 0 first → Y00. `[3:0]` carriage phases, `[7:4]` paper phases, `[8]` R/W=0,
`[15:9]` head elements Y09 (bottom) … Y15 (top). Outputs change on the XC edge.

Power budget: head is 7 × element through ULN2003 at 12 V — measure element R to size
the 12 V supply; original Apple II PSU 12 V rail is ~1.5 A total. The /// used its own
12 V. Start with a 12 V / 2 A bench supply.
