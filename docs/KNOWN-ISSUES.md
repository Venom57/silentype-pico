# Known Issues — audit of the June v0 draft

> This file is an audit trail, oldest first. Earlier sections describe designs since superseded
> (parallel driver board, 74HC123 one-shot, current shunt). The current state is the **v0.6** section at the end.

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

## v0.5 — card ROM, card schematic, PRINTER schematic, and pinout all found

Apple II Documentation Project (mirrors.apple2.org.za/retro/…/Interface Cards/**Serial**/
Apple Synch Printer Interface Card (Silentype)/ — I'd been looking under "Parallel").
Everything is in `docs/firmware-archaeology/` and `docs/reference/`.

| Item | Resolution |
|---|---|
| Card ROM 341-0039-00 | **Found** (2 KB) + Marc Ressl's 2010 commented disassembly `silentype.s` |
| Printer-side schematic 050-0023-01 | **Found.** 74LS673 + 3× ULN2003 + 555 ceiling + margin switch. Complete. |
| Connector | **DB-9**, pinout from Apple KB TA27850. The 26-pin template was wrong. |
| Burn tables | Full 8-level table from the card ROM (549–2744 µs main). Differs 3× from the /// driver at default — bench decides, start low. |
| `MAX_COLS` | 80 chars (`printerRowNum`) → ~560 columns; ruler-check pending |
| Paper geometry | 40 steps/inch; text line = 7 + 2 steps |
| Home sensor | margin switch to GND → active LOW |
| Paper-out sensor | **does not exist** |
| Hardware design | Collapsed to level shifter + pull-up + DB-9. U5 one-shot now optional (printer has 555). |
| Firmware transport | **PIO shift-out implemented** (`silentype_sr.pio`, `shiftreg.c`); engine has zero parallel-GPIO calls |
| TBD_FROM_ROM | 11 → **5** (ramp ×2, MAX_COLS ruler check, reverse offset, rehome cadence) — all bench tunables |

**Stale in the tree:** `hardware/schematic/*.svg` and the generators (v0.3 parallel design),
`docs/reverse-engineering.md` §3 bench table (half its rows are now answered). Both marked.

## v0.6 — code review fixes and carrier v1.0 (2026-11-19)

### Fixed by the review

| # | Issue | Fix |
|---|---|---|
| R1 | Word shifted LSB first — reversed against both Apple drivers; carriage phases would have fired head elements and feed phase 3 would have set Y08 (the 673's R/W̅) | PIO shifts MSB first (`word << 16`, shift-left); `sr_write` forces bit 8 = 0 |
| R2 | Store sequence and the 555's role misunderstood (thought: 555 triggered by SC·XC) | Schematic decode: SC·XC = **clear**; the 555 is a head-on watchdog. Whole transaction moved into PIO with the store done while SC is low |
| R3 | Host tools put the top raster row in bit 0 (the bottom dot) — every band upside down | Row r → bit 6 − r in `silentype-app.c` and `send_band.py`; `protocol.md` documents bit 6 = top |
| R4 | Column period depended on content (blank columns ~4× faster steps) | Fixed slots scheduled against each column's start time; identical period per level |
| R5 | FEED bypassed the queue and ran mid-page; a second FEED overwrote the first | FEED queued in order with bands, back-pressured like BAND; RESET/error discard it |
| R6 | `send_band.py image`/`feed`/`setparam` broken by argparse order | Subcommands; `burn LEVEL US`; CRC-checked replies; missing ACK/NAK reported |
| R7 | SET_PARAM burn ids documented as 0x01 = level 1; firmware uses 0x01 = level 0 | Docs and `protocol.h` corrected (id = 0x01 + level); `PARAM_BURN_US_LVL()` |
| — | PAPPL app never read the FEED ACK, so ACKs paired with the wrong commands | `expect_ack()` checks the echoed command and result for every frame |
| — | Software guard wrote to the PIO from a core-0 IRQ, racing core 1 | Guard drops GP6 (carrier bus enable) instead; printer cleared in hardware |

### Carrier v1.0 (`hardware/pcb/`)

Generated from `build_carrier.py`, routed with Freerouting, DRC clean (0 errors, 0 unconnected),
Gerbers exported. 74AHCT541 buffer; Q1 bus enable on GP6 with SC/XC pull-ups so a released bus
clears the printer (replaces the optional 74HC123 one-shot U5, now dropped); 12 V jack with PTC
and crowbar; R-78E 5 V; Pico 2 / 2 W on sockets with antenna keepout. Pin GP9 (U5 strobe) removed
from the firmware; GP6 = `PIN_BUS_EN` added.

### Still open (PAPPL rows moved to v0.7 below)

| Severity | Area | Issue |
|---|---|---|
| Medium | Firmware | RESET during a paper feed leaves a feed coil energised |
| Medium | Firmware | Paper feed steps every 4 ms; Apple used ~8–11 ms |
| Medium | Firmware | ROM "pre-heat" column is really a post-step settle; ROM burns include ~440 µs shift time. Tables not yet reinterpreted |
| Low | Firmware | Burns set within ~50 µs of the 3000 µs cap trip the guard every column |
| Low | Firmware | Half-received frame survives a USB reconnect; homing reach 735 vs ROM 792 steps; `engine_state` written by both cores |
| Low | Firmware | Feed button (GP22) and error LED (GP26) not yet used |
| Low | Firmware | Dead code: parallel-GPIO pin names, ADC current-sense path |

Bench items (head resistance, 555 time-out, column count, burn level, reverse offset) are tracked
in `docs/bring-up.md`.

## v0.7 — PAPPL app fixed and tested (2026-11-21)

Checked against the PAPPL 1.4.12 headers (`-Wall -Wextra -Werror`, GCC 16). New test
`make -C host/pappl-app test` runs the app's own job and device callbacks, with PAPPL stubbed, against a
fake Pico on a pseudo-terminal: 11 scenarios. Mutation-checked: reverting the bit order or the
job-start RESET makes it fail.

| Severity | Issue | Fix |
|---|---|---|
| Critical | `papplDeviceWrite` buffers and nothing flushed it: no frame was ever sent | Frames written straight to the port (full-write loop); the device write callback also handles partial writes |
| Critical | Device-list callback returned `void`; stop/continue inverted | Returns `bool`; stops when PAPPL's callback returns true; the glob no longer skips macOS ports |
| High | RESET at job start discarded the previous job's queued bands and page feed | Poll STATUS until idle; RESET only to clear an error, then wait for the re-home |
| High | Firmware reported idle while homing after RESET or boot | `engine_state` = busy while homing (`print_engine.c`); `protocol.md` updated |
| High | `PAPPL_RASTER_TYPE_BLACK_1` does not exist (compile error) | `PAPPL_PWG_RASTER_TYPE_BLACK_1` |
| Medium | Media width in wrong units; 60 × 60 dpi; 480 columns | 8.5 × 11 in in hundredths of a mm; 75 × 40 dpi; 560 columns, centred by side margins |
| Medium | A reply split across USB packets read as a time-out | Replies read with a deadline until complete |
| Low | Darkness → intensity 1..5, default 3 | 0..7; `printer-darkness-configured` 14 → intensity 1 |
| Low | Probed every `/dev/ttyACM*` with STATUS | Linux: only ports with USB ID 1209:0001 |
| Low | Raw `format` set with no print-file callback | Removed: raster only |
| Critical | Found on the real server: `ppm` 0, so PAPPL refused to add the printer (reported as a bad printer name) | `ppm = 1` |
| Critical | Found on the real server: without `force_raster_type` PAPPL sends 8-bit lines; read as bits, every page printed blank | `force_raster_type = BLACK_1`; pages that are not 1-bit are refused |

End to end: built against libpappl 1.4.12; an isolated `silentype-app server` added the printer
on a fake Pico and printed a PNG, 63 bands of 560 columns plus the page feed, which decode to
the right image, the right way up and centred.

Still open: PNG/JPEG sent straight to the app print squeezed sideways (PAPPL's image scaler
assumes square pixels; 75 × 40 dpi is not); not yet tried from a desktop print dialog; dpi and
columns await the M1 ruler check; listing on macOS still probes every `cu.usbmodem*` port.
