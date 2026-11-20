# BOM notes — carrier v1.0

`BOM.csv` is generated from the parts list in `pcb/build_carrier.py`, the same data the board
is built from. All parts are through-hole. The circuit is described in `pcb/README.md`.

## Component choices

| Ref | Part | Why this one |
|---|---|---|
| U2 | SN74AHCT541N | TTL-threshold inputs accept 3.3 V; 5 V outputs; flow-through pinout (inputs one side, outputs the other) routes cleanly; OE̅ gives the bus-enable fail-safe |
| Q1 | 2N3904 | Saturates hard from a 3.3 V GPIO through 4.7 kΩ (a 2N7000's 3 V worst-case threshold would not) |
| R4, R5 | 4.7 kΩ to +5V_PRN | Hold SC = XC = 1 (printer cleared) whenever the buffer releases the bus, independent of the printer's own 3 kΩ pull-ups |
| R1–R3 | 47 Ω | Series damping on the cable lines |
| R10, R11, D3 | 1 kΩ, 10 kΩ to 3V3, BAT85 | Margin-switch input that can never put 5 V on the Pico pin |
| U3 | RECOM R-78E5.0-1.0 | Drop-in switching 5 V; a 7805 would dissipate about 1 W from 12 V |
| F1, D1 | MF-RHT200 + 1N5822 | Reverse polarity crowbars the rail and trips the PTC; no diode drop on the head supply in normal use |
| F2 | MF-RHT050 | Limits the printer's 5 V logic feed (expected about 150 mA) |
| D2 | 1N5817 | Board 5 V into Pico VSYS, so USB and the board supply can both be connected |
| C1 | 470 µF 25 V low-ESR | Bulk at the connector; the printer has 3300 µF inside |

## Open hardware questions

- **F1 rating.** 2 A hold is a placeholder until head element resistance and current are
  measured (M1). The original Apple II supply provided about 1.5 A on 12 V in total.
- **Printer pull-up on MS.** Unknown; the BAT85 clamp covers the worst case.
- **J1 gender.** The printer's cable ends in a male DE-9 plug that mated with a socket on the
  Apple II card's bracket, so J1 is a female socket. Confirm on your printer before ordering.

## Power-up discipline

See `docs/bring-up.md`: bare board at 100 mA, then the Pico without the printer, then the
printer at 300 mA, then the 555 check, and only then 2 A and a first column at intensity 0.
