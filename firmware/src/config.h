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

// ---- Pin map (must match hardware/schematic/carrier-board.md) ----
#define PIN_HEAD_FIRST     2                 // GP2.. = dots 1..HEAD_DOTS
#define HEAD_MASK          (((1u << HEAD_DOTS) - 1u) << PIN_HEAD_FIRST)
#define PIN_HEAD_STROBE    9                 // rising edge re-arms U5 one-shot
#define PIN_CARR_FIRST     10
#define PIN_FEED_FIRST     14
#define PIN_HOME_SENSE     20
#define PIN_PAPER_SENSE    21
#define PIN_BTN_FEED       22
#define PIN_LED_ERR        26
#define PIN_ISENSE         27                // ADC1, head return shunt (Option B)
#define ADC_ISENSE_CH      1

#define HOME_SENSE_ACTIVE_LOW  TBD_FROM_ROM(1)   // sensor polarity: bench

// ---- Mechanism constants — values marked ROM: are from the SOS driver ($0A58/$0AC0 etc.) ----
static const uint8_t CARR_PHASE_TABLE[4] = {0x1, 0x8, 0x4, 0x2};   // ROM: one-hot wave drive, LSR ring
#define CARR_STEP_INTERVAL_US    4032                  // ROM: VIA T1 = $0FC0 when stepping without printing
#define CARR_RAMP_STEPS          TBD_FROM_ROM(0)       // ROM has no ramp; keep 0 unless the motor stalls
#define CARR_RAMP_START_US       TBD_FROM_ROM(6000)
#define CARR_HOME_TIMEOUT_MS     3000
#define FEED_STEPS_PER_BAND      TBD_FROM_ROM(7)       // ROM feeds via $0AAE calls; count per text line not yet decoded
#define MAX_COLS                 TBD_FROM_ROM(480)     // ROM derives from page table at $0D92 — not yet decoded
#define FEED_MOTOR_FITTED        1                     // ROM: separate 4-phase ring in $34 ($0A7A/$0A8B)
static const uint8_t FEED_PHASE_TABLE[4] = {0x1, 0x8, 0x4, 0x2};   // ROM: same wave drive
#define FEED_STEP_INTERVAL_US    4032                  // ROM: one step + 2× T1 $0FC0 in $0AAE → ~8 ms/step effective
#define PAPER_SENSE_FITTED       TBD_FROM_ROM(0)      // 1 if a paper-out sensor exists
#define PAPER_SENSE_ACTIVE_LOW   TBD_FROM_ROM(1)

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

// Head burn. ROM uses ONE main burn of 640 µs (+256 µs when the "dark" flag $35 is set),
// preceded by a 512 µs PRE-HEAT applied only to elements that were off for the previous
// two columns, then 2624 µs off. Intensity levels here scale the main burn around 640;
// level 3 = ROM default, level 5 = ROM "dark". Pre-heat is fixed.
#define MAX_INTENSITY 5
static const uint16_t BURN_US[MAX_INTENSITY + 1] = {0, 400, 520, 640, 768, 896};
#define PREHEAT_US               512    // ROM $69/$6A — cold elements only (2-column lookback)
#define BURN_HARD_LIMIT_US       1200   // software guard; must be < U5 one-shot (~1500). ROM max is 896+512.
#define INTER_COL_COOLDOWN_US    2624   // ROM $65/$66 — head-off dwell per column
#define COLUMN_TAIL_US           512    // ROM: trailing settle after the off word

// Head fault thresholds (Option B shunt). 0 disables.
#define ISENSE_MIN_mA_PER_DOT    TBD_FROM_ROM(0)     // below => open element
#define ISENSE_MAX_mA_PER_DOT    TBD_FROM_ROM(0)     // above => short
#define ISENSE_SHUNT_mOHM        100

#define BAND_QUEUE_DEPTH  8
