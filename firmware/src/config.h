// config.h — silentype-pico. Single source of truth: pin map + mechanism timing.
// Every TBD_FROM_ROM is an INVENTED placeholder. They anchor to nothing. Replace
// each with a value extracted from the C800 ROM / SOS driver disassembly or a
// bench measurement (docs/reverse-engineering.md) before the head rail is connected.
#pragma once
#include <stdint.h>
#define TBD_FROM_ROM(...) __VA_ARGS__   // grep-able marker; variadic so brace initializers pass through

// ---- Head geometry — UNVERIFIED. Confirm element count at the connector (M1). ----
#define HEAD_DOTS          TBD_FROM_ROM(7)   // if >8, band column type becomes u16
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

// ---- Mechanism constants ----
static const uint8_t CARR_PHASE_TABLE[4] = TBD_FROM_ROM({0x9, 0xC, 0x6, 0x3});
#define CARR_STEP_INTERVAL_US    TBD_FROM_ROM(2000)   // cruise
#define CARR_RAMP_STEPS          TBD_FROM_ROM(16)     // linear ramp length
#define CARR_RAMP_START_US       TBD_FROM_ROM(5000)   // first step interval
#define CARR_HOME_TIMEOUT_MS     3000
#define FEED_STEPS_PER_BAND      TBD_FROM_ROM(7)
#define MAX_COLS                 TBD_FROM_ROM(480)
#define FEED_MOTOR_FITTED        TBD_FROM_ROM(1)      // 0 = clutch/solenoid off carriage (M1 decides)
static const uint8_t FEED_PHASE_TABLE[4] = TBD_FROM_ROM({0x9, 0xC, 0x6, 0x3});
#define FEED_STEP_INTERVAL_US    TBD_FROM_ROM(3000)
#define PAPER_SENSE_FITTED       TBD_FROM_ROM(0)      // 1 if a paper-out sensor exists
#define PAPER_SENSE_ACTIVE_LOW   TBD_FROM_ROM(1)

// ---- Bidirectional printing ----
// Reverse-pass columns land offset from forward-pass columns because of
// mechanical backlash + burn-vs-step phase. ROM has a compensation value.
#define REVERSE_OFFSET_STEPS     TBD_FROM_ROM(0)      // signed; 0 = disabled
#define REHOME_EVERY_N_BANDS     TBD_FROM_ROM(0)      // 0 = only at start/RESET

// ---- Thermal history compensation (0 = disabled) ----
// heat[i] accumulates per element: heat = heat*HEAT_DECAY/256 + (burned?256:0)
// burn_us is scaled by (1 - HEAT_K * heat / 65536). Original ROM may not do this;
// evaluate after M5 if dense areas print darker than isolated dots.
#define HEAT_DECAY               TBD_FROM_ROM(0)      // 0..255 per column
#define HEAT_K                   TBD_FROM_ROM(0)      // 0..255

// Head burn per intensity, µs. PLACEHOLDERS — not derived from anything.
#define MAX_INTENSITY 5
static const uint16_t BURN_US[MAX_INTENSITY + 1] = TBD_FROM_ROM({0, 300, 450, 600, 800, 1000});
#define BURN_HARD_LIMIT_US       1200   // software guard; must be < U5 one-shot (~1500)
#define INTER_COL_COOLDOWN_US    TBD_FROM_ROM(200)

// Head fault thresholds (Option B shunt). 0 disables.
#define ISENSE_MIN_mA_PER_DOT    TBD_FROM_ROM(0)     // below => open element
#define ISENSE_MAX_mA_PER_DOT    TBD_FROM_ROM(0)     // above => short
#define ISENSE_SHUNT_mOHM        100

#define BAND_QUEUE_DEPTH  8
