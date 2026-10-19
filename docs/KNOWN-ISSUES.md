# Known Issues — audit of the June v0 draft

Status key: **FIXED** in this tree · **OPEN** needs bench/ROM data · **DOC** text corrected

## Code that was broken
| # | Issue | Status |
|---|---|---|
| 1 | `CMakeLists.txt` linked `tinyusb_board` with no `tusb_config.h` — wouldn't build | FIXED — `src/tusb_config.h` added (CDC) |
| 2 | `handle_frame` spun forever on a malformed BAND (same `false` for "full" and "invalid") | FIXED — `engine_queue_band` returns `QUEUE_OK / QUEUE_FULL / QUEUE_INVALID` |
| 3 | Engine kept printing after homing failure | FIXED — error state latches; bands are discarded until RESET |
| 4 | No abort path; RESET couldn't interrupt a band | FIXED — `abort_req` checked per column |
| 5 | `send_band.py` thresholded before resizing, stretched aspect, used `Image.point` on mode "1" | FIXED — convert L → resize w/ pad → threshold |

## Design decisions that were wrong
| # | Issue | Status |
|---|---|---|
| 6 | USB printer class 7 conflicted with PAPPL (macOS class driver claims it; Linux gets a phantom raw queue) | FIXED — USB CDC serial |
| 7 | ULN2003A driven from 3.3 V GPIO (input network designed for 5 V) | FIXED — motor lines go through U6 74AHCT245 first |
| 8 | Flyback diodes D1–D7 on resistive head elements | FIXED — removed; D8–D11 kept for stepper-on-FET option |
| 9 | "Hardware watchdog" was a software timer IRQ | FIXED — added U5 74HC123 monostable gating head '245 OE; software guard kept as second layer and relabeled |
| 10 | `ERR_HEAD_FAULT` defined with no detection mechanism | FIXED (Option B) — 0.1 Ω shunt → GP27/ADC1; OPEN for Option A (current not on carrier) |
| 11 | No stepper acceleration ramp | FIXED — linear ramp table in engine; profile values still `TBD_FROM_ROM` |

## Claims that were false or unverified
| # | Issue | Status |
|---|---|---|
| 12 | "ROM ships in AppleWin/MAME ROM sets" — neither is known to emulate the card | DOC — sources corrected in `reverse-engineering.md` |
| 13 | "Logic board mounts on standoffs in rear of case" — invented | DOC — replaced with "measure" |
| 14 | "Element return is VMOT inside the printer" — assumption as fact | DOC — marked TBD on schematic and signals.md |
| 15 | 7-element head — unverified, load-bearing | OPEN — M1 verification row added; `HEAD_DOTS` is the single knob |
| 16 | `BURN_US` placeholders claimed "~50% of ROM values" with ROM unknown | DOC — comment now says "invented; replace" |
| 17 | README/protocol described bidirectional; engine was forward-only | FIXED — engine alternates direction; `REVERSE_OFFSET_STEPS` is TBD_FROM_ROM, tunable live via SET_PARAM 0x12 |

## Gaps closed in v0.3
| Item | How |
|---|---|
| Thermal-history compensation | per-element heat accumulator in `print_engine.c`; `HEAT_DECAY`/`HEAT_K` default 0 (off) until M5 shows it's needed |
| Reverse-pass offset | implemented; value TBD_FROM_ROM, live-tunable |
| Paper-out → PAPPL | engine checks sensor per band → `ERR_PAPER_OUT`; PAPPL status cb maps to `media-empty` |
| Paper feed motor | implemented for the two-motor case (`FEED_MOTOR_FITTED=1`); clutch variant still unknowable until Trendcom docs |
| SET_PARAM | full bench-tuning table, RAM only, burn values capped at the software ceiling |
| PAPPL app | callbacks implemented: black_1 raster → band packing, device probe/list, status mapping, print-darkness → intensity |
| Service files | udev rule, systemd unit, launchd plist |

## Still open — need bench or documentation, not code
- `HEAD_DOTS` verification (M1)
- Every `TBD_FROM_ROM` in `config.h` (M0/M1)
- Paper advance mechanism if it is NOT a second motor
- Head-fault detection under Option A (physically impossible on the carrier)
- Monostable is retriggerable: a hang *in a retrigger loop* with lines high defeats it. BOM lists 74HC221 as the non-retriggerable alternative.
- PAPPL app written against the 1.4 API from memory — expect field-name fixes on first compile; the band-packing and protocol logic is what matters

## v0.4 — Apple /// SOS driver found (docs/firmware-archaeology/)

The Silentype /// driver disk (Apple 681-0009-A) was located on apple3.org and
disassembled. This resolves or reframes several items:

| Item | Resolution |
|---|---|
| #15 HEAD_DOTS unverified | **CONFIRMED 7** — font table max $7F |
| #16 BURN_US invented | **Real values**: 640 µs main (+256 dark), 512 µs pre-heat on cold elements, 2624 µs off, 512 µs tail |
| Decision 1 (drivers where?) | **Option A, stronger than expected**: the printer takes a 16-bit *serial* word (8 head bits + 4 motor phase bits) via shift register + 2 strobes. The printer has latch AND drivers. |
| Decision 2 (motors) | Two 4-phase rings (`$33` carriage, `$34` feed) → `FEED_MOTOR_FITTED=1` |
| Thermal compensation | ROM does 2-column lookback pre-heat. Engine now implements exactly that; accumulator model demoted to optional. |
| Bidirectional | Confirmed in ROM, with a per-column −4 µs timing trim on reverse passes rather than a step offset |
| Stepper | One-hot wave drive `{1,8,4,2}`, 4032 µs/step when not printing |

**Design consequence not yet applied to hardware docs/diagrams:** the carrier should
drive SDATA/SCLK/STROBE_A/STROBE_B + read SENSE — ~5 wires — not 7+8 parallel lines.
`print_engine.c` still writes parallel GPIO; a PIO shift-out transport is the next firmware
change (M2 blocker). Schematic/pinout SVGs and BOM are **stale** against this finding
and will be regenerated once the ribbon pinout is known (M1). TBD_FROM_ROM count: 18 → 11.
