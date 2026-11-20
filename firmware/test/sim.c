// sim.c — virtual-clock implementation of the stubbed SDK + a recording sr_write().
#include <setjmp.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "sim.h"
#include "tusb.h"
#include "config.h"
#include "shiftreg.h"

#define SIM_LOG_MAX 200000
sim_write_t sim_log[SIM_LOG_MAX];
size_t      sim_nlog;
uint64_t    sim_now = 1000;
unsigned    sim_sr_cost_us = 23;
unsigned    sim_guard_fired;

static bool             alarm_active;
static uint64_t         alarm_deadline;
static alarm_callback_t alarm_cb;

static void advance_to(uint64_t t) {
    if (t <= sim_now) return;
    if (alarm_active && t >= alarm_deadline) {        // guard expires mid-wait
        sim_now = alarm_deadline; alarm_active = false; sim_guard_fired++;
        alarm_cb(1, NULL);
    }
    if (t > sim_now) sim_now = t;
}

absolute_time_t get_absolute_time(void)            { return (absolute_time_t){ sim_now }; }
absolute_time_t make_timeout_time_ms(uint32_t ms)  { return (absolute_time_t){ sim_now + (uint64_t)ms * 1000u }; }
void busy_wait_us(uint64_t us)                     { advance_to(sim_now + us); }
void busy_wait_until(absolute_time_t t)            { advance_to(t._private_us_since_boot); }

alarm_id_t add_alarm_in_us(uint64_t us, alarm_callback_t cb, void *u, bool f) {
    (void)u; (void)f;
    if (alarm_active) { fprintf(stderr, "sim: nested guard alarm\n"); exit(2); }
    alarm_active = true; alarm_deadline = sim_now + us; alarm_cb = cb;
    return 1;
}
bool cancel_alarm(alarm_id_t id) {
    (void)id;
    if (alarm_active && sim_now >= alarm_deadline) sim_guard_fired++;
    bool was = alarm_active; alarm_active = false; return was;
}

void gpio_init(uint p) { (void)p; }
void gpio_set_dir(uint p, bool o) { (void)p; (void)o; }
void gpio_put(uint p, bool v) { (void)p; (void)v; }
bool gpio_get(uint p) { (void)p; return false; }      // margin switch active-low: always "home"
void gpio_pull_up(uint p) { (void)p; }
uint16_t adc_read(void) { return 0; }
void stdio_uart_init(void) {}

// ---- shift register: record every latched word ----
void sr_init(void) { sr_write(0); }
uint16_t sr_make(uint8_t head7, uint8_t carr4, uint8_t feed4) {
    return (uint16_t)(((head7 & 0x7Fu) << SR_HEAD_SHIFT) | ((feed4 & 0xFu) << SR_FEED_SHIFT) | (carr4 & 0xFu));
}
void sr_write(uint16_t w) {
    advance_to(sim_now + sim_sr_cost_us);
    if (sim_nlog == SIM_LOG_MAX) { fprintf(stderr, "sim: log overflow\n"); exit(2); }
    sim_log[sim_nlog++] = (sim_write_t){ sim_now, w };
}
void sim_clear_log(void) { sim_nlog = 0; }

// ---- core1: run engine_loop until it idles, then longjmp back ----
static void  (*core1_entry)(void);
static jmp_buf idle_jmp;
static bool    idle_armed;
void multicore_launch_core1(void (*entry)(void)) { core1_entry = entry; }
void tight_loop_contents(void) { if (idle_armed) longjmp(idle_jmp, 1); }
// NOTE: each call re-enters engine_loop from the top (latch(0) + re-home, which
// resets carriage position/direction), so queue everything a scenario needs first.
void sim_run_core1(void) {
    if (!setjmp(idle_jmp)) { idle_armed = true; core1_entry(); }
    idle_armed = false;
}

// ---- TinyUSB CDC ----
uint8_t  cdc_rx[1 << 16], cdc_tx[1 << 16];
uint32_t cdc_rx_len, cdc_rx_pos, cdc_tx_len;
void     tusb_init(void) {}
void     tud_task(void) {}
bool     tud_cdc_connected(void) { return true; }
uint32_t tud_cdc_available(void) { return cdc_rx_len - cdc_rx_pos; }
uint32_t tud_cdc_read(void *b, uint32_t n) {
    uint32_t a = cdc_rx_len - cdc_rx_pos; if (n > a) n = a;
    memcpy(b, cdc_rx + cdc_rx_pos, n); cdc_rx_pos += n; return n;
}
uint32_t tud_cdc_write(const void *b, uint32_t n) { memcpy(cdc_tx + cdc_tx_len, b, n); cdc_tx_len += n; return n; }
uint32_t tud_cdc_write_flush(void) { return 0; }
