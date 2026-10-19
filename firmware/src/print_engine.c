// print_engine.c — core1 real-time mechanism control.
// Owns every GPIO that can burn paper or stall a motor. core0 talks to it only
// through the band queue and volatile request flags. No USB, malloc or printf here.
//
// Head safety is TWO layers:
//   1. HARDWARE: U5 (74HC123) one-shot gates U2 '245 OE. head_burn() strobes GP9
//      before raising dot lines; if this core hangs with lines high, U5 times out
//      (~1.5 ms), U2 tri-states, pulldowns turn the head off. Firmware cannot
//      defeat this.
//   2. SOFTWARE: a timer-IRQ guard (core0 alarm pool, survives a core1 hang)
//      force-clears head GPIOs past BURN_HARD_LIMIT_US. Catches bugs.
// Layer 2 is NOT a hardware watchdog and must not be described as one.

#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "hardware/adc.h"
#include "config.h"
#include "print_engine.h"

typedef struct {
    uint16_t width;
    uint8_t  intensity, feed_steps, flags;
    uint8_t  cols[MAX_COLS];
} band_t;

static band_t   queue[BAND_QUEUE_DEPTH];
static volatile uint32_t q_head, q_tail;
static volatile uint32_t bands_printed;
static volatile uint8_t  engine_state;              // 0 idle 1 printing 2 error
static volatile uint8_t  engine_error;
static volatile uint16_t head_mA_last;

static volatile bool     req_reset, req_selftest, abort_req;
static volatile uint16_t req_feed_steps;

// ---- runtime-tunable copies of config constants (SET_PARAM, RAM only) --------
static uint16_t burn_us[MAX_INTENSITY + 1];
static uint32_t step_interval_us_cfg = CARR_STEP_INTERVAL_US;
static uint32_t cooldown_us          = INTER_COL_COOLDOWN_US;
static int32_t  reverse_offset       = REVERSE_OFFSET_STEPS;
static uint16_t feed_steps_default   = FEED_STEPS_PER_BAND;
static uint8_t  heat_decay = HEAT_DECAY, heat_k = HEAT_K;

static inline void set_error(uint8_t e) { engine_error = e; engine_state = 2; }

// ---- software guard ----------------------------------------------------------
static alarm_id_t guard_id;
static int64_t guard_cb(alarm_id_t id, void *user) { (void)id; (void)user; gpio_clr_mask(HEAD_MASK); return 0; }

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
static uint8_t prev1, prev2;

static inline bool pulse(uint32_t bits, uint16_t us, uint16_t *mA_out) {
    if (!bits || !us) { if (mA_out) *mA_out = 0; return true; }
    if (us > BURN_HARD_LIMIT_US) us = BURN_HARD_LIMIT_US;
    gpio_put(PIN_HEAD_STROBE, 1);                                 // re-arm U5
    guard_id = add_alarm_in_us(BURN_HARD_LIMIT_US, guard_cb, NULL, true);
    gpio_set_mask(bits);
    busy_wait_us(us / 2);
    uint16_t mA = read_head_mA();
    busy_wait_us(us - us / 2);
    gpio_clr_mask(HEAD_MASK);
    cancel_alarm(guard_id);
    gpio_put(PIN_HEAD_STROBE, 0);
    if (mA_out) *mA_out = mA;
    return true;
}

static bool head_burn(uint8_t col_bits, uint16_t burn) {
    col_bits &= (1u << HEAD_DOTS) - 1u;
    burn = heat_adjust(col_bits, burn);
    uint8_t  cold = col_bits & (uint8_t)~(prev1 | prev2);
    uint32_t sh   = PIN_HEAD_FIRST;
    uint16_t mA   = 0;

    pulse((uint32_t)cold << sh, PREHEAT_US, NULL);                // word 1: pre-heat
    pulse((uint32_t)col_bits << sh, burn, &mA);                   // word 2: main burn
    // word 3 (head off) is the inter-column cooldown, applied by the caller.

    prev2 = prev1; prev1 = col_bits;
    head_mA_last = mA;
    heat_update(col_bits);
    int nd = __builtin_popcount(col_bits);
    if (nd && ISENSE_MIN_mA_PER_DOT && mA < nd * ISENSE_MIN_mA_PER_DOT) return false;
    if (nd && ISENSE_MAX_mA_PER_DOT && mA > nd * ISENSE_MAX_mA_PER_DOT) return false;
    return true;
}
static inline void head_history_reset(void) { prev1 = prev2 = 0; }

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
    gpio_put_masked(0xFu << PIN_CARR_FIRST, (uint32_t)CARR_PHASE_TABLE[carr_phase_idx] << PIN_CARR_FIRST);
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
        gpio_put_masked(0xFu << PIN_FEED_FIRST, (uint32_t)FEED_PHASE_TABLE[feed_phase_idx] << PIN_FEED_FIRST);
        busy_wait_us(FEED_STEP_INTERVAL_US);
    }
    gpio_put_masked(0xFu << PIN_FEED_FIRST, 0);     // de-energize; holding torque not needed
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
    uint8_t  lvl  = (b->intensity >= 1 && b->intensity <= MAX_INTENSITY) ? b->intensity : 1;
    uint16_t burn = burn_us[lvl];
    bool forced_fwd = b->flags & 0x01;
    int  dir = forced_fwd ? +1 : carr_dir;

    if (dir > 0) {
        if (carr_pos != 0) carr_move(-carr_pos);                // forced forward: go home first
        head_history_reset(); motion_begin(); printing_pass = true;
        for (uint16_t c = 0; c < b->width; c++) {
            if (abort_req) { printing_pass = false; return; }
            if (!head_burn(b->cols[c], burn)) { printing_pass = false; set_error(ERR_HEAD_FAULT); return; }
            busy_wait_us(cooldown_us + COLUMN_TAIL_US);
            carr_step(+1);
        }
        printing_pass = false; carr_dir = -1;
    } else {
        // Reverse pass: carriage is at (prev width) steps; walk back to this band's
        // right edge + offset, then print right-to-left.
        int32_t target = (int32_t)b->width + reverse_offset;
        if (carr_pos != target) carr_move(target - carr_pos);
        head_history_reset(); motion_begin(); printing_pass = true;
        for (int32_t c = (int32_t)b->width - 1; c >= 0; c--) {
            if (abort_req) { printing_pass = false; return; }
            carr_step(-1);
            if (!head_burn(b->cols[c], burn)) { printing_pass = false; set_error(ERR_HEAD_FAULT); return; }
            busy_wait_us(cooldown_us + COLUMN_TAIL_US);
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
    band_t b = { .width = 200, .intensity = 1, .feed_steps = 0, .flags = 0 };
    for (int i = 0; i < 200; i++) b.cols[i] = (i & 1) ? 0x55 : 0x2A;
    print_band(&b);
    if (engine_state != 2) print_band(&b);           // second band exercises the reverse pass
}

// ---- core1 loop ----------------------------------------------------------------
static void engine_loop(void) {
    gpio_clr_mask(HEAD_MASK);
    if (!carriage_home()) set_error(ERR_HOME_TIMEOUT);

    for (;;) {
        if (req_reset) {
            abort_req = false; req_reset = false;
            gpio_clr_mask(HEAD_MASK);
            q_tail = q_head;
            memset(heat, 0, sizeof heat);
            engine_error = ERR_NONE; engine_state = 0;
            if (!carriage_home()) set_error(ERR_HOME_TIMEOUT);
            continue;
        }
        if (engine_state == 2) {
            if (q_head != q_tail) q_tail = q_head;
            req_feed_steps = 0; req_selftest = false;
            tight_loop_contents(); continue;
        }
        if (req_feed_steps) { uint16_t n = req_feed_steps; req_feed_steps = 0; paper_feed(n); continue; }
        if (req_selftest)   { req_selftest = false; engine_state = 1; selftest_pattern(); if (engine_state != 2) engine_state = 0; continue; }
        if (q_head == q_tail) { engine_state = 0; tight_loop_contents(); continue; }

        engine_state = 1;
        print_band(&queue[q_tail % BAND_QUEUE_DEPTH]);
        __dmb();
        q_tail++;
        if (engine_state != 2) bands_printed++;
    }
}

// ---- core0-facing API ------------------------------------------------------------
void engine_init(void) {
    memcpy(burn_us, BURN_US, sizeof burn_us);
    for (int i = 0; i < HEAD_DOTS; i++) { gpio_init(PIN_HEAD_FIRST + i); gpio_set_dir(PIN_HEAD_FIRST + i, GPIO_OUT); gpio_put(PIN_HEAD_FIRST + i, 0); }
    gpio_init(PIN_HEAD_STROBE); gpio_set_dir(PIN_HEAD_STROBE, GPIO_OUT); gpio_put(PIN_HEAD_STROBE, 0);
    for (int i = 0; i < 4; i++) {
        gpio_init(PIN_CARR_FIRST + i); gpio_set_dir(PIN_CARR_FIRST + i, GPIO_OUT);
        gpio_init(PIN_FEED_FIRST + i); gpio_set_dir(PIN_FEED_FIRST + i, GPIO_OUT);
    }
    gpio_init(PIN_HOME_SENSE);  gpio_set_dir(PIN_HOME_SENSE, GPIO_IN);
    gpio_init(PIN_PAPER_SENSE); gpio_set_dir(PIN_PAPER_SENSE, GPIO_IN);
    adc_init(); adc_gpio_init(PIN_ISENSE); adc_select_input(ADC_ISENSE_CH);
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
void engine_request_reset(void)          { abort_req = true; req_reset = true; }
void engine_request_feed(uint16_t steps) { req_feed_steps = steps; }
void engine_request_selftest(void)       { req_selftest = true; }
void engine_set_error(uint8_t err)       { set_error(err); }

bool engine_set_param(uint8_t id, uint32_t v) {
    if (engine_state == 1) return false;                    // not while printing
    if (id >= PARAM_BURN_US_1 && id < PARAM_BURN_US_1 + MAX_INTENSITY) {
        if (v == 0 || v > BURN_HARD_LIMIT_US) return false;  // never above the software ceiling
        burn_us[id - PARAM_BURN_US_1 + 1] = (uint16_t)v; return true;
    }
    switch (id) {
    case PARAM_STEP_INTERVAL_US: if (v < 300 || v > 20000) return false; step_interval_us_cfg = v; return true;
    case PARAM_COOLDOWN_US:      if (v > 20000) return false; cooldown_us = v; return true;
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
