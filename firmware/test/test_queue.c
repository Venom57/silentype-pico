// test_queue.c — Bug 5: FEED is queued in order with bands, back-pressured like a
// band, rejected in error state, discarded by RESET / error. Drives main.c's real
// frame parser (pump_rx) through a fake CDC FIFO.
#include <stdio.h>
#include <string.h>
#define main fw_main
#include "main.c"            // real parser, static functions visible
#undef main
#include "sim.h"

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

// ---- host side: frames in, ACKs out ----
static void put_frame(uint8_t cmd, const uint8_t *p, uint16_t len) {
    uint8_t *f = cdc_rx + cdc_rx_len;
    f[0] = PROTO_MAGIC; f[1] = cmd; f[2] = len & 0xFF; f[3] = len >> 8;
    memcpy(f + 4, p, len);
    f[4 + len] = crc8_07(f + 1, 3 + len);
    cdc_rx_len += 5u + len;
}
static void put_band(uint16_t w, uint8_t feed) {
    uint8_t p[sizeof(band_hdr_t) + 64];
    band_hdr_t h = { .width_cols = w, .intensity = 1, .feed_steps = feed, .flags = 0, .reserved = 0 };
    memcpy(p, &h, sizeof h);
    for (uint16_t i = 0; i < w; i++) p[sizeof h + i] = (i % 3) ? 0x7F : 0x00;
    put_frame(CMD_BAND, p, (uint16_t)(sizeof h + w));
}
static void put_feed(uint16_t steps) { uint8_t p[2] = { steps & 0xFF, steps >> 8 }; put_frame(CMD_FEED, p, 2); }

// Pops ACKs from the tx capture into acks[] as (cmd<<8 | result). Returns count.
static int take_acks(uint16_t *acks, int max) {
    int n = 0; uint32_t i = 0;
    while (i + 7 <= cdc_tx_len && n < max) {
        CHECK(cdc_tx[i] == PROTO_MAGIC && cdc_tx[i + 1] == RSP_ACK, "unexpected reply frame");
        acks[n++] = (uint16_t)(cdc_tx[i + 4] << 8 | cdc_tx[i + 5]);
        i += 7;
    }
    cdc_tx_len = 0;
    return n;
}
static void reset_io(void) { cdc_rx_len = cdc_rx_pos = cdc_tx_len = 0; rxlen = 0; }

// ---- mechanism event trace from the shift-register log ----
// 'C' = carriage step, 'F' = paper step (feed phase energized); de-energize ignored.
static int trace(char *out, int max) {
    int n = 0; uint16_t prev = sim_nlog ? sim_log[0].word : 0;
    for (size_t i = 1; i < sim_nlog && n < max - 1; i++) {
        uint16_t w = sim_log[i].word;
        if ((w & 0xF) != (prev & 0xF)) out[n++] = 'C';
        uint8_t f = (w >> SR_FEED_SHIFT) & 0xF;
        if (f && f != ((prev >> SR_FEED_SHIFT) & 0xF)) out[n++] = 'F';
        prev = w;
    }
    out[n] = 0;
    return n;
}
static void expect(char *buf, const char *spec) {   // spec: "10C3F" → CCCCCCCCCCFFF
    int k = 0, num = 0;
    for (const char *s = spec; *s; s++) {
        if (*s >= '0' && *s <= '9') { num = num * 10 + (*s - '0'); continue; }
        for (int i = 0; i < num; i++) buf[k++] = *s;
        num = 0;
    }
    buf[k] = 0;
}
static void check_trace(const char *name, const char *spec) {
    static char got[100000], want[100000];
    trace(got, sizeof got); expect(want, spec);
    bool ok = strcmp(got, want) == 0;
    printf("  %s: %s (%zu events)\n", name, ok ? "order OK" : "ORDER MISMATCH", strlen(got));
    CHECK(ok, "%s: trace\n   got  %s\n   want %s", name, got, want);
}
static status_t status(void) { status_t s; engine_fill_status(&s); return s; }

int main(void) {
    uint16_t a[32]; int n;
    engine_init();
    sim_run_core1();                                  // initial home, idle
    reset_io();

    // 1. BAND, BAND, FEED, BAND all queued before core1 runs.
    put_band(10, 3); put_band(10, 3); put_feed(40); put_band(10, 3);
    pump_rx();
    n = take_acks(a, 32);
    CHECK(n == 4, "1: %d acks", n);
    for (int i = 0; i < n; i++) CHECK((a[i] & 0xFF) == ACK_OK, "1: ack %d result %u", i, a[i] & 0xFF);
    CHECK(status().buffer_bands == 4, "1: buffer %u", status().buffer_bands);
    uint32_t printed0 = status().bands_printed;
    sim_clear_log(); sim_run_core1();
    check_trace("BAND,BAND,FEED,BAND", "10C3F10C3F40F10C3F");
    CHECK(status().bands_printed - printed0 == 3, "1: bands_printed +%u (feed must not count)", status().bands_printed - printed0);
    CHECK(status().buffer_bands == 0, "1: queue not drained");

    // 2. Back-pressure: 8 bands fill the ring; FEED is not ACKed and stays in rx.
    reset_io();
    for (int i = 0; i < BAND_QUEUE_DEPTH; i++) put_band(4, 1);
    put_feed(40); put_band(4, 1);
    pump_rx();
    n = take_acks(a, 32);
    CHECK(n == BAND_QUEUE_DEPTH, "2: %d acks before drain, expected %d", n, BAND_QUEUE_DEPTH);
    CHECK(rxlen > 0, "2: FEED frame should be held in rx");
    sim_clear_log(); sim_run_core1();                // drain the 8 bands
    pump_rx();                                       // FEED + last band now queue
    n = take_acks(a, 32);
    CHECK(n == 2 && a[0] == (CMD_FEED << 8 | ACK_OK) && a[1] == (CMD_BAND << 8 | ACK_OK), "2: late acks n=%d", n);
    sim_run_core1();
    check_trace("8xBAND (full), FEED, BAND", "4C1F4C1F4C1F4C1F4C1F4C1F4C1F4C1F40F4C1F");

    // 3. Invalid length / zero steps.
    reset_io();
    { uint8_t p[3] = { 1, 0, 0 }; put_frame(CMD_FEED, p, 1); put_frame(CMD_FEED, p, 3); }
    put_feed(0);
    pump_rx();
    n = take_acks(a, 32);
    CHECK(n == 3, "3: %d acks", n);
    CHECK(n >= 3 && (a[0] & 0xFF) == ACK_INVALID && (a[1] & 0xFF) == ACK_INVALID, "3: bad length not INVALID");
    CHECK(n >= 3 && (a[2] & 0xFF) == ACK_OK, "3: zero feed not OK");
    CHECK(status().buffer_bands == 0, "3: zero feed used a slot");
    printf("  invalid-length FEED -> ACK_INVALID, FEED 0 -> ACK_OK no-op\n");

    // 4. RESET discards a queued FEED.
    reset_io();
    put_band(5, 2); put_feed(40);
    pump_rx(); take_acks(a, 32);
    engine_request_reset();
    sim_clear_log(); sim_run_core1();
    check_trace("RESET discards BAND+FEED", "");
    CHECK(status().buffer_bands == 0 && status().state == 0, "4: not reset");

    // 5. Error state: FEED rejected with ERRSTATE; already-queued feeds discarded.
    reset_io();
    put_feed(40); pump_rx(); take_acks(a, 32);
    engine_set_error(ERR_HEAD_FAULT);
    put_feed(40); pump_rx();
    n = take_acks(a, 32);
    CHECK(n == 1 && (a[0] & 0xFF) == ACK_ERRSTATE, "5: FEED in error state not ERRSTATE");
    sim_clear_log(); sim_run_core1();
    check_trace("error state discards FEED", "");
    CHECK(status().buffer_bands == 0, "5: queue not discarded");
    engine_request_reset(); sim_run_core1();

    printf(failures ? "test_queue: %d FAILURE(S)\n" : "test_queue: OK\n", failures);
    return failures != 0;
}
