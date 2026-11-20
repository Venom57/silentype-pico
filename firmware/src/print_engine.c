// print_engine.c — core1 real-time mechanism control.
// Owns every GPIO that can burn paper or stall a motor. core0 talks to it only
// through the band queue (bands and FEEDs, executed strictly in order) and volatile
// request flags (RESET, SELFTEST). No USB, malloc or printf here.
//
// All outputs go through the printer's 74LS673 via sr_write() (shiftreg.c). Nothing
// here touches a head or motor line directly.
//
// Head safety:
//   1. HARDWARE (inside the printer): a 555 watchdog on the head driver outputs. If any
//      element stays on ~5-6 ms continuously it clears the whole 74LS673 latch (motors too).
//      Stores do not retrigger it; it re-arms once all elements are off (HARDWARE-INTERFACE §2).
//   2. HARDWARE (carrier v1.0): bus enable (GP6 -> Q1 -> '541 OE). Dropping it releases the
//      bus and the carrier pull-ups hold SC=XC=1, clearing every latch output at once.
//   3. SOFTWARE: a timer-IRQ guard (core0 alarm pool, survives a core1 hang) latches
//      an all-off word past BURN_HARD_LIMIT_US. Catches bugs.
// Layer 3 is NOT a hardware watchdog and must not be described as one.

#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "hardware/adc.h"
#include "config.h"
#include "print_engine.h"
#include "shiftreg.h"

// One queue entry. width == 0 marks a feed-only entry (CMD_FEED): no columns are
// printed, the paper advances feed_steps (> 0) and bands_printed is not incremented.
// Queuing FEED through the same ring keeps it in order with the bands before it.
// (Same sizeof as before: the u16 feed_steps fills what used to be padding.)
typedef struct {
    uint16_t width;                                 // 0 = feed-only entry
    uint16_t feed_steps;                            // band: advance after band, 0 = default; feed-only: steps
    uint8_t  intensity, flags;
    uint8_t  cols[MAX_COLS];
} band_t;

static band_t   queue[BAND_QUEUE_DEPTH];
static volatile uint32_t q_head, q_tail;
static volatile uint32_t bands_printed;
static volatile uint8_t  engine_state;              // 0 idle 1 printing 2 error
static volatile uint8_t  engine_error;
static volatile uint16_t head_mA_last;
static uint8_t cur_carr, cur_feed;                 // latched motor phases (persist across head pulses)
static inline void latch(uint8_t head7) { sr_write(sr_make(head7, cur_carr, cur_feed)); }

static volatile bool     req_reset, req_selftest, abort_req;

// ---- runtime-tunable copies of config constants (SET_PARAM, RAM only) --------
static uint16_t burn_us[INTENSITY_LEVELS], preheat_us[INTENSITY_LEVELS], off_us[INTENSITY_LEVELS];
static uint32_t step_interval_us_cfg = CARR_STEP_INTERVAL_US;
static int32_t  reverse_offset       = REVERSE_OFFSET_STEPS;
static uint16_t feed_steps_default   = FEED_STEPS_PER_BAND;
static uint8_t  heat_decay = HEAT_DECAY, heat_k = HEAT_K;

static inline void set_error(uint8_t e) { engine_error = e; engine_state = 2; }

// ---- software guard ----------------------------------------------------------
static alarm_id_t guard_id;
static volatile bool guard_tripped;
// Runs on core0 (alarm IRQ). Must not touch the PIO: core1 may be mid-sr_write. Dropping the
// bus enable is one SIO write and clears the printer's outputs in hardware.
static int64_t guard_cb(alarm_id_t id, void *user) {
    (void)id; (void)user; sr_bus_enable(false); guard_tripped = true; return 0;
}

// ---- head --------------------------------------------------------------------
static uint16_t heat[HEAD_DOTS];                   // thermal history, 0..65535

static uint16_t read_head_mA(void) {
#if ISENSE_MIN_mA_PER_DOT || ISENSE_MAX_mA_PER_DOT
    uint32_t raw = adc_read();
    return (uint16_t)((raw * 3300000u / 4096u) / ISENSE_SHUNT_mOHM);
#else
    return 0;
#endif
}

static inline uint16_t heat_adjust(uint8_t col_bits, uint16_t burn) {
    if (!heat_k) return burn;
    // scale by the hottest element being fired this column
    uint32_t hmax = 0;
    for (int i = 0; i < HEAD_DOTS; i++) if ((col_bits >> i) & 1 && heat[i] > hmax) hmax = heat[i];
    uint32_t reduce = burn * hmax / 65536u * heat_k / 256u;
    return (uint16_t)(reduce < burn / 2 ? burn - reduce : burn / 2);   // never below 50 %
}
static inline void heat_update(uint8_t col_bits) {
    if (!heat_decay) return;
    for (int i = 0; i < HEAD_DOTS; i++) {
        uint32_t h = heat[i] * heat_decay / 256u;
        if ((col_bits >> i) & 1) h += 8192;            // one burn
        heat[i] = h > 65535 ? 65535 : (uint16_t)h;
    }
}

// ROM column sequence (SOS driver $0B7B): pre-heat cold elements 512 µs, burn all
// elements BURN_US, then INTER_COL_COOLDOWN_US off. "Cold" = off for the previous two
// columns. prev1/prev2 persist across columns within a pass and reset at band start.
//
// Column timing is CONTENT-INDEPENDENT. The carriage stepper is open-loop and steps
// once per column, so the column period IS its step interval; the Apple ROMs spend
// the same time on every column whatever its dots. Each column is a fixed schedule
// against absolute time from its start t0:
//
//   [t0, t0+PRE)            pre-heat slot: cold elements on (head off if none cold)
//   [t0+PRE, t0+PRE+BURN)   burn slot: all elements on for the heat_adjust()'d burn,
//                           head off for the rest of the slot
//   [t0+PRE+BURN, t0+P)     off slot: head off
//
//   PRE  = min(preheat_us[lvl], BURN_HARD_LIMIT_US)
//   BURN = min(burn_us[lvl],    BURN_HARD_LIMIT_US)
//   P    = PRE + BURN + max(off_us[lvl] + COLUMN_TAIL_US, MIN_OFF_SLOT_US)
//
// Slots are scheduled from t0, not by summing busy-waits, so sr_write() latency does
// not accumulate. A blank column spends the whole period with the head off.
//
// TODO(timing): PREHEAT_US_TAB is probably a post-step settle delay in the ROM, not a
// cold-element pre-heat. Kept as-is here (separate known issue).
static uint8_t prev1, prev2;

// Floor on the off slot: the head-off write (one sr_write, ~20 µs) plus any pre-heat
// overrun must land inside the column even if SET_PARAM sets the cooldown to 0.
#define MIN_OFF_SLOT_US 100u

static inline uint32_t slot_preheat_us(uint8_t lvl) { return preheat_us[lvl] < BURN_HARD_LIMIT_US ? preheat_us[lvl] : BURN_HARD_LIMIT_US; }
static inline uint32_t slot_burn_us(uint8_t lvl)    { return burn_us[lvl]    < BURN_HARD_LIMIT_US ? burn_us[lvl]    : BURN_HARD_LIMIT_US; }
static inline uint32_t column_period_us(uint8_t lvl) {
    uint32_t off = (uint32_t)off_us[lvl] + COLUMN_TAIL_US;
    return slot_preheat_us(lvl) + slot_burn_us(lvl) + (off > MIN_OFF_SLOT_US ? off : MIN_OFF_SLOT_US);
}

// One head-on pulse: waits for t_on, latches head7, holds it `us` from the moment
// the on-write began, then latches the head off. Length is measured from the actual
// on-write (not t_on), so a late start never shortens the burn; the overrun is
// absorbed by the following slot. Writes nothing for an empty mask — the caller's
// absolute schedule still spends the slot's time, with the head off.
static void pulse(uint8_t head7, absolute_time_t t_on, uint16_t us, uint16_t *mA_out) {
    if (mA_out) *mA_out = 0;
    if (!head7 || !us) return;
    if (us > BURN_HARD_LIMIT_US) us = BURN_HARD_LIMIT_US;
    busy_wait_until(t_on);
    guard_id = add_alarm_in_us(BURN_HARD_LIMIT_US, guard_cb, NULL, true);
    absolute_time_t on = get_absolute_time();
    latch(head7);                                                 // dots ON
    busy_wait_until(delayed_by_us(on, us / 2));
    uint16_t mA = read_head_mA();
    busy_wait_until(delayed_by_us(on, us));
    latch(0);                                                     // dots OFF
    cancel_alarm(guard_id);
    if (mA_out) *mA_out = mA;
}

// Burns one column that starts at t0. Returns with the head off, inside the off slot;
// the caller waits for the column end (column_end()) before the next step.
static bool head_burn(uint8_t col_bits, uint8_t lvl, absolute_time_t t0) {
    col_bits &= (1u << HEAD_DOTS) - 1u;
    uint16_t burn = heat_adjust(col_bits, burn_us[lvl]);          // <= burn slot; rest of slot is head-off
    uint8_t  cold = col_bits & (uint8_t)~(prev1 | prev2);
    uint16_t mA   = 0;
    uint32_t pre  = slot_preheat_us(lvl);

    pulse(cold,     t0,                     (uint16_t)pre, NULL); // word 1: pre-heat cold elements
    pulse(col_bits, delayed_by_us(t0, pre), burn,          &mA);  // word 2: main burn, all elements
    // word 3 (head off) fills the rest of the column; the caller waits it out.

    prev2 = prev1; prev1 = col_bits;
    head_mA_last = mA;
    heat_update(col_bits);
    if (guard_tripped) return false;                              // burn overran: bus is down
    int nd = __builtin_popcount(col_bits);
    if (nd && ISENSE_MIN_mA_PER_DOT && mA < nd * ISENSE_MIN_mA_PER_DOT) return false;
    if (nd && ISENSE_MAX_mA_PER_DOT && mA > nd * ISENSE_MAX_mA_PER_DOT) return false;
    return true;
}
static inline void head_history_reset(void) { prev1 = prev2 = 0; }

// Waits for the end of the column that started at t0 and returns it (= the next
// column's start). If the column overran, the next one starts now: a late column is
// stretched, never followed by a compressed one (an open-loop stepper tolerates a
// slow step, not a fast one).
static absolute_time_t column_end(absolute_time_t t0, uint32_t period_us) {
    absolute_time_t t_end = delayed_by_us(t0, period_us);
    absolute_time_t now   = get_absolute_time();
    if (absolute_time_diff_us(now, t_end) < 0) return now;
    busy_wait_until(t_end);
    return t_end;
}

// ---- motion ------------------------------------------------------------------
static uint32_t carr_phase_idx, feed_phase_idx;
static int32_t  carr_pos;
static int      carr_dir = +1;                      // direction of the NEXT pass
static uint32_t ramp_pos;

static inline uint32_t step_interval_us(void) {
#if CARR_RAMP_STEPS > 0
    if (ramp_pos >= CARR_RAMP_STEPS) return step_interval_us_cfg;
    return CARR_RAMP_START_US - (CARR_RAMP_START_US - step_interval_us_cfg) * ramp_pos / CARR_RAMP_STEPS;
#else
    return step_interval_us_cfg;   // ROM has no ramp
#endif
}
static void motion_begin(void) { ramp_pos = 0; }
static bool printing_pass;   // when true, carr_step does not add its own dwell (column timing governs)
static void carr_step(int dir) {
    carr_phase_idx = (carr_phase_idx + (dir > 0 ? 1 : 3)) & 3;
    cur_carr = CARR_PHASE_TABLE[carr_phase_idx];
    latch(0);                                                     // ROM steps with head off
    carr_pos += dir;
    if (!printing_pass) busy_wait_us(step_interval_us());
    ramp_pos++;
}
static void carr_move(int32_t steps) {               // signed, with ramp
    motion_begin();
    int dir = steps < 0 ? -1 : +1;
    for (int32_t i = 0; i < (steps < 0 ? -steps : steps); i++) { if (abort_req) return; carr_step(dir); }
}
static inline bool at_home(void)  { bool v = gpio_get(PIN_HOME_SENSE);  return HOME_SENSE_ACTIVE_LOW ? !v : v; }
static inline bool paper_out(void) {
    if (!PAPER_SENSE_FITTED) return false;
    bool v = gpio_get(PIN_PAPER_SENSE); return PAPER_SENSE_ACTIVE_LOW ? !v : v;
}
static bool carriage_home(void) {
    absolute_time_t deadline = make_timeout_time_ms(CARR_HOME_TIMEOUT_MS);
    motion_begin();
    while (!at_home()) {
        carr_step(-1);
        if (abort_req || absolute_time_diff_us(get_absolute_time(), deadline) < 0) return false;
    }
    carr_pos = 0; carr_dir = +1;
    return true;
}
static void paper_feed(uint32_t steps) {
#if FEED_MOTOR_FITTED
    for (uint32_t i = 0; i < steps; i++) {
        if (abort_req) return;
        feed_phase_idx = (feed_phase_idx + 1) & 3;
        cur_feed = FEED_PHASE_TABLE[feed_phase_idx];
        latch(0);
        busy_wait_us(FEED_STEP_INTERVAL_US);
    }
    cur_feed = 0; latch(0);                        // de-energize; holding torque not needed
#else
    // TODO(M1): clutch/solenoid variant — mechanism unknown until the Trendcom docs
    // say how paper advance is driven. Cannot be implemented from here.
    (void)steps;
#endif
}

// ---- band printing — bidirectional ---------------------------------------------
static uint32_t bands_since_home;

static void print_band(const band_t *b) {
    if (paper_out()) { set_error(ERR_PAPER_OUT); return; }
    uint8_t  lvl  = b->intensity <= MAX_INTENSITY ? b->intensity : DEFAULT_INTENSITY;
    uint32_t period = column_period_us(lvl);                    // same for every column of the band
    bool forced_fwd = b->flags & 0x01;
    int  dir = forced_fwd ? +1 : carr_dir;

    // One carriage step per column, always at a column boundary and always with the
    // head off (carr_step latches head=0), so step-to-step time == period both ways.
    if (dir > 0) {
        if (carr_pos != 0) carr_move(-carr_pos);                // forced forward: go home first
        head_history_reset(); motion_begin(); printing_pass = true;
        absolute_time_t t0 = get_absolute_time();
        for (uint16_t c = 0; c < b->width; c++) {
            if (abort_req) { printing_pass = false; return; }
            if (!head_burn(b->cols[c], lvl, t0)) { printing_pass = false; set_error(ERR_HEAD_FAULT); return; }
            t0 = column_end(t0, period);
            carr_step(+1);                                      // step at end of column
        }
        printing_pass = false; carr_dir = -1;
    } else {
        // Reverse pass: carriage is at (prev width) steps; walk back to this band's
        // right edge + offset, then print right-to-left.
        int32_t target = (int32_t)b->width + reverse_offset;
        if (carr_pos != target) carr_move(target - carr_pos);
        head_history_reset(); motion_begin(); printing_pass = true;
        absolute_time_t t0 = get_absolute_time();
        for (int32_t c = (int32_t)b->width - 1; c >= 0; c--) {
            if (abort_req) { printing_pass = false; return; }
            carr_step(-1);                                      // step at start of column
            if (!head_burn(b->cols[c], lvl, t0)) { printing_pass = false; set_error(ERR_HEAD_FAULT); return; }
            t0 = column_end(t0, period);
        }
        printing_pass = false; carr_dir = +1;
    }
    paper_feed(b->feed_steps ? b->feed_steps : feed_steps_default);

    if (REHOME_EVERY_N_BANDS && ++bands_since_home >= (uint32_t)REHOME_EVERY_N_BANDS) {
        bands_since_home = 0;
        if (!carriage_home()) set_error(ERR_HOME_TIMEOUT);
    }
}

static void selftest_pattern(void) {
    band_t b = { .width = 200, .intensity = DEFAULT_INTENSITY, .feed_steps = 0, .flags = 0 };
    for (int i = 0; i < 200; i++) b.cols[i] = (i & 1) ? 0x55 : 0x2A;
    print_band(&b);
    if (engine_state != 2) print_band(&b);           // second band exercises the reverse pass
}

// ---- core1 loop ----------------------------------------------------------------
static void engine_loop(void) {
    latch(0);
    if (!carriage_home()) set_error(ERR_HOME_TIMEOUT);

    for (;;) {
        if (req_reset) {
            abort_req = false; req_reset = false;
            guard_tripped = false; sr_bus_enable(true);  // re-enable after a guard trip
            latch(0);
            q_tail = q_head;
            memset(heat, 0, sizeof heat);
            engine_error = ERR_NONE; engine_state = 0;
            if (!carriage_home()) set_error(ERR_HOME_TIMEOUT);
            continue;
        }
        if (engine_state == 2) {
            if (q_head != q_tail) q_tail = q_head;           // discards queued bands AND feeds
            req_selftest = false;
            tight_loop_contents(); continue;
        }
        if (req_selftest)   { req_selftest = false; engine_state = 1; selftest_pattern(); if (engine_state != 2) engine_state = 0; continue; }
        if (q_head == q_tail) { engine_state = 0; tight_loop_contents(); continue; }

        engine_state = 1;
        const band_t *b = &queue[q_tail % BAND_QUEUE_DEPTH];
        bool is_band = b->width != 0;                     // read before q_tail++ frees the slot
        if (is_band) print_band(b);
        else         paper_feed(b->feed_steps);           // FEED, in order with the bands before it
        __dmb();
        q_tail++;
        if (is_band && engine_state != 2) bands_printed++;
    }
}

// ---- core0-facing API ------------------------------------------------------------
void engine_init(void) {
    memcpy(burn_us, BURN_US, sizeof burn_us); memcpy(preheat_us, PREHEAT_US_TAB, sizeof preheat_us); memcpy(off_us, OFF_US, sizeof off_us);
    gpio_init(PIN_MS); gpio_set_dir(PIN_MS, GPIO_IN); gpio_pull_up(PIN_MS);      // margin switch to GND
    sr_init();                                       // bus enable + latches all-off word
    multicore_launch_core1(engine_loop);
}

queue_result_t engine_queue_band(const uint8_t *payload, uint16_t len) {
    if (engine_state == 2) return QUEUE_ERRSTATE;
    if (len < sizeof(band_hdr_t)) return QUEUE_INVALID;
    const band_hdr_t *h = (const band_hdr_t *)payload;
    if (h->width_cols == 0 || h->width_cols > MAX_COLS || len < sizeof *h + h->width_cols) return QUEUE_INVALID;
    if (q_head - q_tail >= BAND_QUEUE_DEPTH) return QUEUE_FULL;
    band_t *b = &queue[q_head % BAND_QUEUE_DEPTH];
    b->width = h->width_cols; b->intensity = h->intensity; b->feed_steps = h->feed_steps; b->flags = h->flags;
    memcpy(b->cols, payload + sizeof *h, h->width_cols);
    __dmb();
    q_head++;
    return QUEUE_OK;
}
// FEED payload: u16 steps (LE). Queued as a feed-only entry so it runs after every
// band queued before it, and back-pressures like a band when the ring is full.
// A 0-step FEED is a no-op: QUEUE_OK without using a slot (nothing to order).
queue_result_t engine_queue_feed(const uint8_t *payload, uint16_t len) {
    if (engine_state == 2) return QUEUE_ERRSTATE;
    if (len != 2) return QUEUE_INVALID;
    uint16_t steps = (uint16_t)(payload[0] | (payload[1] << 8));
    if (steps == 0) return QUEUE_OK;
    if (q_head - q_tail >= BAND_QUEUE_DEPTH) return QUEUE_FULL;
    band_t *b = &queue[q_head % BAND_QUEUE_DEPTH];
    b->width = 0; b->feed_steps = steps; b->intensity = 0; b->flags = 0;
    __dmb();
    q_head++;
    return QUEUE_OK;
}
void engine_request_reset(void)          { abort_req = true; req_reset = true; }
void engine_request_selftest(void)       { req_selftest = true; }
void engine_set_error(uint8_t err)       { set_error(err); }

bool engine_set_param(uint8_t id, uint32_t v) {
    if (engine_state == 1) return false;                    // not while printing
    if (id >= PARAM_BURN_US_1 && id < PARAM_BURN_US_1 + INTENSITY_LEVELS) {   // 0x01..0x08 = levels 0..7
        if (v == 0 || v > BURN_HARD_LIMIT_US) return false;  // never above the software ceiling
        burn_us[id - PARAM_BURN_US_1] = (uint16_t)v; return true;
    }
    switch (id) {
    case PARAM_STEP_INTERVAL_US: if (v < 300 || v > 20000) return false; step_interval_us_cfg = v; return true;
    case PARAM_COOLDOWN_US:      if (v > 20000) return false; for (int i = 0; i < INTENSITY_LEVELS; i++) off_us[i] = (uint16_t)v; return true;
    case PARAM_REVERSE_OFFSET:   if ((int32_t)v < -50 || (int32_t)v > 50) return false; reverse_offset = (int32_t)v; return true;
    case PARAM_FEED_STEPS:       if (v == 0 || v > 255) return false; feed_steps_default = (uint16_t)v; return true;
    case PARAM_HEAT_DECAY:       if (v > 255) return false; heat_decay = (uint8_t)v; return true;
    case PARAM_HEAT_K:           if (v > 255) return false; heat_k = (uint8_t)v; return true;
    default: return false;
    }
}
void engine_fill_status(status_t *s) {
    s->state = engine_state; s->error_code = engine_error;
    s->buffer_bands = (uint8_t)(q_head - q_tail); s->buffer_max = BAND_QUEUE_DEPTH;
    s->bands_printed = bands_printed; s->uptime_ms = to_ms_since_boot(get_absolute_time());
    s->head_mA_last = head_mA_last; s->reserved = 0;
}
