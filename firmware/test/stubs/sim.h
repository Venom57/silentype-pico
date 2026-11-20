// sim.h — minimal Pico SDK surface for building the firmware on the host.
// Virtual clock: time only moves when the code waits or calls sr_write().
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef unsigned int uint;

// Opaque like the SDK's PICO_OPAQUE_ABSOLUTE_TIME_T build: catches raw arithmetic.
typedef struct { uint64_t _private_us_since_boot; } absolute_time_t;
static inline absolute_time_t delayed_by_us(absolute_time_t t, uint64_t us) { t._private_us_since_boot += us; return t; }
static inline int64_t absolute_time_diff_us(absolute_time_t from, absolute_time_t to) {
    return (int64_t)(to._private_us_since_boot - from._private_us_since_boot);
}
static inline uint32_t to_ms_since_boot(absolute_time_t t) { return (uint32_t)(t._private_us_since_boot / 1000u); }
absolute_time_t get_absolute_time(void);
absolute_time_t make_timeout_time_ms(uint32_t ms);
void busy_wait_us(uint64_t us);
void busy_wait_until(absolute_time_t t);
void tight_loop_contents(void);
static inline void __dmb(void) { __sync_synchronize(); }

#define GPIO_OUT 1
#define GPIO_IN  0
void gpio_init(uint pin);
void gpio_set_dir(uint pin, bool out);
void gpio_put(uint pin, bool v);
bool gpio_get(uint pin);
void gpio_pull_up(uint pin);

typedef int32_t alarm_id_t;
typedef int64_t (*alarm_callback_t)(alarm_id_t id, void *user_data);
alarm_id_t add_alarm_in_us(uint64_t us, alarm_callback_t cb, void *user_data, bool fire_if_past);
bool       cancel_alarm(alarm_id_t id);

uint16_t adc_read(void);
void     multicore_launch_core1(void (*entry)(void));
void     stdio_uart_init(void);

// ---- simulation control / observation ----
typedef struct { uint64_t t; uint16_t word; } sim_write_t;   // t = time the word latched
extern sim_write_t sim_log[];
extern size_t      sim_nlog;
extern uint64_t    sim_now;
extern unsigned    sim_sr_cost_us;       // sr_write latency (shift + store clock)
extern unsigned    sim_guard_fired;      // software guard alarm expiries
void sim_run_core1(void);                // run core1 until it goes idle (tight_loop_contents)
void sim_clear_log(void);
