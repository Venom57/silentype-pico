// test_timing.c — Bug 4: every printed column must take the same time for a given
// intensity, whatever its dots (blank / full / cold / warm), in both directions.
// Uses only the public engine API, so it also builds against older engine sources.
#include <stdio.h>
#include <string.h>
#include "sim.h"
#include "config.h"
#include "protocol.h"
#include "print_engine.h"

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define W 48
static uint8_t pattern[W];

static void make_pattern(void) {
    // blank runs, full columns, isolated dots (cold), repeated dots (warm), alternation
    static const uint8_t p[] = { 0x00, 0x00, 0x7F, 0x7F, 0x7F, 0x00, 0x01, 0x00, 0x00, 0x01,
                                 0x55, 0x2A, 0x55, 0x2A, 0x00, 0x00, 0x00, 0x7F, 0x40, 0x40,
                                 0x40, 0x00, 0x7F, 0x00, 0x7F, 0x08, 0x1C, 0x3E, 0x7F, 0x3E,
                                 0x1C, 0x08, 0x00, 0x00, 0x00, 0x00, 0x7F, 0x7F, 0x7F, 0x7F,
                                 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x00 };
    _Static_assert(sizeof p == W, "pattern width");
    memcpy(pattern, p, W);
}

static void queue(uint8_t intensity) {
    uint8_t buf[sizeof(band_hdr_t) + W];
    band_hdr_t h = { .width_cols = W, .intensity = intensity, .feed_steps = 2, .flags = 0, .reserved = 0 };
    memcpy(buf, &h, sizeof h); memcpy(buf + sizeof h, pattern, W);
    CHECK(engine_queue_band(buf, sizeof buf) == QUEUE_OK, "queue band");
}

static int32_t off_override = -1;                 // mirrors a PARAM_COOLDOWN_US set by the test
static uint32_t expected_period(uint8_t lvl) {
    uint32_t pre = PREHEAT_US_TAB[lvl] < BURN_HARD_LIMIT_US ? PREHEAT_US_TAB[lvl] : BURN_HARD_LIMIT_US;
    uint32_t brn = BURN_US[lvl] < BURN_HARD_LIMIT_US ? BURN_US[lvl] : BURN_HARD_LIMIT_US;
    uint32_t off = (off_override >= 0 ? (uint32_t)off_override : OFF_US[lvl]) + COLUMN_TAIL_US;
    if (off < 100) off = 100;                     // MIN_OFF_SLOT_US
    return pre + brn + off;
}

#define HEAD(w) ((uint8_t)((w) >> SR_HEAD_SHIFT) & 0x7F)
#define CARR(w) ((w) & 0xF)
#define FEED(w) (((w) >> SR_FEED_SHIFT) & 0xF)

// Collect carriage-step times (words where the carriage phase changes) and check
// the global invariants: head off on every word that changes a motor phase, and
// no head-on interval longer than BURN_HARD_LIMIT_US.
// Durations of head-on pulses are gathered into a small histogram (dur/cnt).
#define NDUR 32
static uint32_t dur[NDUR], cnt[NDUR]; static int ndur;
static void note_dur(uint32_t d) {
    for (int i = 0; i < ndur; i++) if (dur[i] == d) { cnt[i]++; return; }
    if (ndur < NDUR) { dur[ndur] = d; cnt[ndur++] = 1; }
}
static size_t steps(uint64_t *t, size_t max, unsigned *n_pulses) {
    size_t n = 0; uint16_t prev = sim_nlog ? sim_log[0].word : 0; uint64_t on_at = 0;
    *n_pulses = 0; ndur = 0;
    for (size_t i = 1; i < sim_nlog; i++) {
        uint16_t w = sim_log[i].word;
        if (CARR(w) != CARR(prev) || FEED(w) != FEED(prev))
            CHECK(HEAD(w) == 0, "motor phase changed with head on (word %zu)", i);
        if (CARR(w) != CARR(prev) && n < max) t[n++] = sim_log[i].t;
        if (HEAD(w) && !HEAD(prev)) on_at = sim_log[i].t;
        if (!HEAD(w) && HEAD(prev)) {
            uint64_t d = sim_log[i].t - on_at; (*n_pulses)++;
            CHECK(d <= BURN_HARD_LIMIT_US, "head on %llu us", (unsigned long long)d);
            note_dur((uint32_t)d);
        }
        CHECK(!(HEAD(w) && HEAD(prev)), "head word replaced without an off word between (word %zu)", i);
        prev = w;
    }
    return n;
}

static void check_bands(const char *name, const uint8_t *lvls, int nb) {
    static uint64_t t[64 * W];
    unsigned pulses;
    size_t n = steps(t, sizeof t / sizeof t[0], &pulses);
    CHECK(n == (size_t)nb * W, "%s: %zu carriage steps, expected %d", name, n, nb * W);
    for (int b = 0; b < nb && (size_t)(b + 1) * W <= n; b++) {
        uint32_t want = expected_period(lvls[b]), lo = UINT32_MAX, hi = 0;
        for (int c = 1; c < W; c++) {
            uint32_t d = (uint32_t)(t[b * W + c] - t[b * W + c - 1]);
            if (d < lo) lo = d;
            if (d > hi) hi = d;
        }
        printf("  %s band %d (%s, level %u): step-to-step %u..%u us, expected %u\n", name, b,
               b & 1 ? "reverse" : "forward", lvls[b], lo, hi, want);
        CHECK(lo == hi, "%s band %d: column period varies %u..%u us", name, b, lo, hi);
        CHECK(lo == want, "%s band %d: period %u != %u", name, b, lo, want);
    }
    printf("  %s: %u head-on pulses, guard fired %u; pulse lengths us(count):", name, pulses, sim_guard_fired);
    for (int i = 0; i < ndur; i++) printf(" %u(%u)", dur[i], cnt[i]);
    printf("\n");
    CHECK(sim_guard_fired == 0, "%s: software guard fired", name);
}

int main(void) {
    make_pattern();
    engine_init();

    // Scenario 1: levels 1, 7, 0 — each printed forward then reverse.
    static const uint8_t lv1[] = { 1, 1, 7, 7, 0, 0 };
    for (unsigned i = 0; i < sizeof lv1; i++) queue(lv1[i]);
    sim_run_core1();
    check_bands("tables", lv1, sizeof lv1);

    // Scenario 2: thermal history on, so heat_adjust() shortens burns. Period must not move.
    CHECK(engine_set_param(PARAM_HEAT_DECAY, 230), "set heat decay");
    CHECK(engine_set_param(PARAM_HEAT_K, 255), "set heat k");
    sim_clear_log();
    static const uint8_t lv2[] = { 3, 3 };
    for (unsigned i = 0; i < sizeof lv2; i++) queue(lv2[i]);
    sim_run_core1();
    check_bands("heat_adjust", lv2, sizeof lv2);

    // Scenario 3: cooldown 0 via SET_PARAM (off slot floor) — still uniform.
    CHECK(engine_set_param(PARAM_HEAT_K, 0), "heat k off");
    CHECK(engine_set_param(PARAM_COOLDOWN_US, 0), "cooldown 0");
    off_override = 0;
    sim_clear_log();
    static const uint8_t lv3[] = { 7, 7 };
    for (unsigned i = 0; i < sizeof lv3; i++) queue(lv3[i]);
    sim_run_core1();
    check_bands("cooldown0", lv3, sizeof lv3);

    printf(failures ? "test_timing: %d FAILURE(S)\n" : "test_timing: OK\n", failures);
    return failures != 0;
}
