# silentype-pico

USB controller for the **Apple Silentype** (Trendcom 200 mechanism) thermal printer,
built on a Raspberry Pi Pico 2, with a host-side PAPPL printer application that
presents it to macOS/Linux as an IPP Everywhere printer (zero driver install).

## Why this works

The Silentype has no brains. Apple removed the Trendcom controller's CPU/ROM and
drove the mechanism from the Apple II via the interface card (firmware by Andy
Hertzfeld, in the card's C800 ROM). The printer side is drivers, a thermal head on
a stepper-driven carriage, paper feed, and sensors. The Pico replaces the card and
the 6502 and becomes the controller.

## Architecture

```
 macOS / Linux app
        │  (print dialog)
        ▼
      CUPS ──IPP──► silentype-app (PAPPL, host/pappl-app/)
                        │  PWG raster → 1-bit → N-dot bands
                        ▼  USB CDC serial, framed band protocol
                  Pico 2 firmware (firmware/)
                        │  core0: TinyUSB CDC + frame parser
                        │  core1: real-time print engine
                        ▼
                  Carrier board (hardware/)
                        │  monostable head gate, FETs/level shift,
                        │  stepper drivers, sensor dividers, shunt
                        ▼
                  Silentype 26-pin connector (printer unmodified)
```

## Repo map

| Path | Contents |
|---|---|
| `docs/KNOWN-ISSUES.md` | Audit trail: v0 flaws → v0.2 fixes → v0.3 gap closures, and what's still open |
| `docs/reverse-engineering.md` | Phase 0 checklist + measurement log (DO THIS FIRST) |
| `docs/signals.md` | 26-pin connector signal map (template) |
| `docs/protocol.md` | Serial band protocol spec (host ⇄ Pico) |
| `hardware/BOM.csv` / `BOM.md` | Electronics BOM + the decisions that gate it |
| `hardware/schematic/` | Net-level schematic description, SVG schematic, pinout diagram, generators |
| `firmware/` | Pico SDK project (CMake), TinyUSB CDC + print engine |
| `host/pappl-app/` | PAPPL printer application skeleton |
| `host/tools/` | pyserial test tools (no CUPS needed) |
| `host/linux/`, `host/macos/` | udev rule, launchd plist |

## Milestones

- [ ] **M0** Manual + card schematic acquired; C800 ROM (or Apple /// SOS driver) disassembled; `config.h` has zero `TBD_FROM_ROM`
- [ ] **M1** Bench characterization complete — **including head element count**
- [ ] **M2** Carrier board built; sensors readable; monostable gate verified on scope
- [ ] **M3** Carriage homes and sweeps (with ramp) under Pico control
- [ ] **M4** Single column burns cleanly at conservative timing
- [ ] **M5** Fixed test pattern band prints (forward pass)
- [ ] **M6** Serial band protocol end-to-end via `host/tools/send_band.py`
- [ ] **M7** PAPPL app prints PWG raster; visible in macOS/Linux print dialog
- [ ] **M8** `lp -d silentype file.pdf` produces paper
- [ ] **M9** Tune `REVERSE_OFFSET_STEPS` (bidirectional code is in; the number isn't)

## Safety model (read before powering anything)

Two independent layers protect the head — the only irreplaceable part:

1. **Hardware ceiling (U5, 74HC123 monostable).** The head level-shifter's output
   enable is driven by a one-shot that the firmware must re-trigger per column.
   If the Pico hangs with dot lines high, the one-shot times out (~1.5 ms), the
   '245 tri-states, and the 10 k pulldowns turn every element off. Firmware
   cannot defeat this.
2. **Software guard** (`print_engine.c`). A timer IRQ force-clears head GPIOs if
   a burn exceeds `BURN_HARD_LIMIT_US`. Catches logic bugs; not a substitute for 1.

Plus: gate pulldowns on every head line, head rail fused, 0.1 Ω shunt + ADC for
open/short detection (Option B), bench supply current-limited at first power-up.

## Things this README does NOT assert

- Head element count. Everything says 7 because that's the common claim; it is
  **unverified** and load-bearing (band byte layout). Confirm at the connector in M1.
- Burn times. `config.h` placeholders are invented. They anchor to nothing until M0.
- Where the element return is wired. Assumed in printer; confirm.
