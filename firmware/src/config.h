// config.h — silentype-pico. Single source of truth: pin map + mechanism timing.
// Every TBD_FROM_ROM is an INVENTED placeholder. They anchor to nothing. Replace
// each with a value extracted from the C800 ROM / SOS driver disassembly or a
// bench measurement (docs/reverse-engineering.md) before the head rail is connected.
#pragma once
#include <stdint.h>
#define TBD_FROM_ROM(...) __VA_ARGS__   // grep-able marker; variadic so brace initializers pass through

// ---- Head geometry — CONFIRMED from Apple /// SOS driver font table (docs/firmware-archaeology) ----
#define HEAD_DOTS          7                 // glyph columns never exceed $7F
#if HEAD_DOTS > 8
#error "band protocol column byte assumes HEAD_DOTS <= 8"
#endif

// ---- Pin map — SERIAL interface to the printer's 74LS673 (see HARDWARE-INTERFACE.md) ----
// DB-9: 4=SD, 9=SC, 8=XC, 5=MS. The printer contains latch + ULN2003 drivers + 555 ceiling.
#define PIN_SD             2                 // serial data  → DB9-4 (via level shifter)
#define PIN_SC             3                 // shift clock  → DB9-9 (rising edge shifts)
#define PIN_XC             4                 // store clock  → DB9-8 (edge latches all 16 outputs)
#define PIN_MS             5                 // margin switch ← DB9-5 (active low; pull-up)
#define PIN_HEAD_STROBE    9                 // optional carrier one-shot re-arm (U5) — printer already has a 555
#define PIN_HOME_SENSE     PIN_MS
#define PIN_PAPER_SENSE    21                // unused — no paper sensor on this printer
#define PIN_BTN_FEED       22
#define PIN_LED_ERR        26
#define PIN_ISENSE         27                // unused — current never crosses the carrier
#define ADC_ISENSE_CH      1

// Legacy parallel-GPIO names kept so print_engine.c compiles until the PIO transport lands.
#define PIN_HEAD_FIRST     10
#define HEAD_MASK          (((1u << HEAD_DOTS) - 1u) << PIN_HEAD_FIRST)
#define PIN_CARR_FIRST     17
#define PIN_FEED_FIRST     21

// 74LS673 word layout: bit0 shifted first → Y00
#define SR_CARR_SHIFT      0                 // bits 0-3 carriage phases
#define SR_FEED_SHIFT      4                 // bits 4-7 paper phases
#define SR_RW_BIT          8                 // write 0
#define SR_HEAD_SHIFT      9                 // bits 9-15 head elements (Y09 bottom … Y15 top)

#define HOME_SENSE_ACTIVE_LOW  1                 // schematic 050-0023-01: margin switch to GND

// ---- Mechanism constants — values marked ROM: are from the SOS driver ($0A58/$0AC0 etc.) ----
static const uint8_t CARR_PHASE_TABLE[4] = {0x1, 0x8, 0x4, 0x2};   // ROM: one-hot wave drive, LSR ring
#define CARR_STEP_INTERVAL_US    4032                  // ROM: VIA T1 = $0FC0 when stepping without printing
#define CARR_RAMP_STEPS          TBD_FROM_ROM(0)       // ROM has no ramp; keep 0 unless the motor stalls
#define CARR_RAMP_START_US       TBD_FROM_ROM(6000)
#define CARR_HOME_TIMEOUT_MS     3000
#define FEED_STEPS_PER_BAND      7                     // contiguous graphics; text adds LINE_SPACING_STEPS
#define LINE_SPACING_STEPS       2                     // manual: default line increment (CF0E)
#define PAPER_STEPS_PER_INCH     40                    // manual p.21: form feed 40 steps ≈ 1 inch
#define MAX_COLS                 TBD_FROM_ROM(560)     // 80 chars × 7 cols (card ROM printerRowNum=80); ruler-check M1
#define FEED_MOTOR_FITTED        1                     // ROM: separate 4-phase ring in $34 ($0A7A/$0A8B)
static const uint8_t FEED_PHASE_TABLE[4] = {0x1, 0x8, 0x4, 0x2};   // ROM: same wave drive
#define FEED_STEP_INTERVAL_US    4032                  // ROM: one step + 2× T1 $0FC0 in $0AAE → ~8 ms/step effective
#define PAPER_SENSE_FITTED       0                     // schematic: no paper-out sensor exists
#define PAPER_SENSE_ACTIVE_LOW   1

// ---- Bidirectional printing ----
// Reverse-pass columns land offset from forward-pass columns because of
// mechanical backlash + burn-vs-step phase. ROM has a compensation value.
#define REVERSE_OFFSET_STEPS     TBD_FROM_ROM(0)      // ROM trims via a per-column -4 µs timing adder on reverse passes, not a step offset. Bench.
#define REHOME_EVERY_N_BANDS     TBD_FROM_ROM(0)      // 0 = only at start/RESET

// ---- Thermal history compensation ----
// The ROM's scheme: pre-heat pulse only for elements cold ≥2 columns (see PREHEAT_US).
// That is what print_engine.c implements. The accumulator model (HEAT_DECAY/HEAT_K) is
// retained as an optional extra, default off.
#define HEAT_DECAY               0
#define HEAT_K                   0

// Head burn — two independent Apple sources disagree on magnitude:
//   Apple /// SOS driver:   main 640 µs (+256 dark), pre-heat 512, off 2624
//   Apple II card ROM 341-0039-00, 8 levels (docs/firmware-archaeology/HARDWARE-INTERFACE.md §3):
//     lvl:      0    1    2    3    4    5*   6    7       (*default)
//     main:   549  823 1098 1372 1646 1921 2195 2744 µs
//     preheat 735  588  470  294  196   98   24    5 µs   (cold elements only)
//     off    1225 1078  960  784  686  588  514  495 µs
// Levels 0..7 below follow the Apple II card table. Level 1 ≈ the Apple /// value.
// START AT LEVEL 1 ON THE BENCH and go up. Printer's own 555 (~6 ms) is the last-resort ceiling.
#define MAX_INTENSITY 7
#define INTENSITY_LEVELS (MAX_INTENSITY + 1)
static const uint16_t BURN_US   [MAX_INTENSITY + 1] = { 549,  823, 1098, 1372, 1646, 1921, 2195, 2744 };
static const uint16_t PREHEAT_US_TAB[MAX_INTENSITY + 1] = { 735,  588,  470,  294,  196,   98,   24,    5 };
static const uint16_t OFF_US    [MAX_INTENSITY + 1] = {1225, 1078,  960,  784,  686,  588,  514,  495 };
#define DEFAULT_INTENSITY        1      // deliberately below Apple's default 5 until the head is characterized
#define BURN_HARD_LIMIT_US       3000   // software guard; above table max, below printer's 555 (~6 ms)
#define COLUMN_TAIL_US           0      // Apple II card has no separate tail; /// had 512

// Head current sense: NOT POSSIBLE on this printer (drivers are internal). Kept at 0 = disabled.
#define ISENSE_MIN_mA_PER_DOT    0
#define ISENSE_MAX_mA_PER_DOT    0
#define ISENSE_SHUNT_mOHM        100

#define PREHEAT_US               PREHEAT_US_TAB[DEFAULT_INTENSITY]   // engine will switch to per-level tables
#define INTER_COL_COOLDOWN_US    OFF_US[DEFAULT_INTENSITY]

#define BAND_QUEUE_DEPTH  8
