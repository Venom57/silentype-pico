# Test patterns

- M2: `send_band.py status` — proves the CDC link and frame parser before any motion.
- M2: bus-enable check — scope SC and XC (TP2, TP3) with the printer connected: with GP6 low or the
  Pico held in reset they must sit at 5 V (printer cleared); after boot, one 16-bit word per
  `sr_write` with the store done while SC is low. Head rail disconnected.
- M4: single-dot ladder, one element per column; scope the drive; burn width == BURN_US.
- M5: `send_band.py pattern` — alternating 0x55/0x2A columns, every element, no sustained burns.
- M7 prep: `send_band.py image gradient.png`.

Full procedure with pass criteria: `docs/bring-up.md`.
