# Carrier board — superseded

The carrier is now designed in `hardware/pcb/` (v1.0): a generated, routed, DRC-clean KiCad
board with Gerbers. See `hardware/pcb/README.md` for the circuit and `hardware/BOM.csv` for parts.

The files in this directory are historical:

- `carrier-schematic.svg`, `pinout-connection.svg` and their generators `gen_schematic.py`,
  `gen_pinout.py` show the v0.3 design: a 15-line parallel driver board with a 74HC123
  monostable, FETs and a current shunt. That design was abandoned once the printer schematic
  showed the printer already contains its own shift register, drivers and watchdog.
- The v0.5 plan (74AHCT125 + pull-up + DE-9) became v1.0 with a 74AHCT541, a bus-enable
  fail-safe and an on-board 5 V regulator.
