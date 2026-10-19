# Test patterns

- M2: `send_band.py status` — proves the CDC link and frame parser before any motion.
- M2: U5 gate check — scope on a U2 output while a *test build* holds one dot line
  high after a single strobe; output must drop at ~1.5 ms. Head rail disconnected.
- M4: single-dot ladder, one element per column; scope the drive; burn width == BURN_US.
- M5: `send_band.py pattern` — alternating 0x55/0x2A columns, every element, no sustained burns.
- M7 prep: `send_band.py image gradient.png`.
