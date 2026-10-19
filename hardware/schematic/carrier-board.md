# Carrier Board — v0.5 (serial interface)

**The SVGs in this directory are from v0.3 and are WRONG for the real interface.**
They show a 15-line parallel driver board. The real board is below; the generators
will be rewritten once the bench confirms the DB-9 wiring (M1).

```
                 ┌────────────────────────┐
  USB ◄─────────►│ Pico 2 (U1)            │
                 │  GP2 SD  ──► U2a ──────┼──► J1-4  SD
                 │  GP3 SC  ──► U2b ──────┼──► J1-9  SC
                 │  GP4 XC  ──► U2c ──────┼──► J1-8  XC   (optionally via U5 one-shot)
                 │  GP5 MS  ◄── R2 100R ──┼──◄ J1-5  MS   (R1 10k pull-up to 3V3)
                 │  GP9 STROBE (opt→U5)   │
                 │  GP0/1 UART            │
                 └────────────────────────┘
   +5V  ─F2─┬─ U2 VCC ─ J1-2            J1-3 Logic GND ─┐
            └─ C8                        J1-7 Power GND ─┴─ GND (star at J3)
   +12V ─F1─── C7 ──────── J1-6          J1-1 Shield → chassis
```

U2 = 74AHCT125 (OE̅ tied low). Nothing else. Place U2 right at J1.

Pin assignment mirrors `firmware/src/config.h` (`PIN_SD/SC/XC/MS`).
