// shiftreg.c — PIO shift-out + CPU-driven store clock.
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/gpio.h"
#include "config.h"
#include "shiftreg.h"
#include "silentype_sr.pio.h"

static PIO  pio = pio0;
static uint sm, offset;

void sr_init(void) {
    gpio_init(PIN_XC); gpio_set_dir(PIN_XC, GPIO_OUT); gpio_put(PIN_XC, 0);
    offset = pio_add_program(pio, &silentype_sr_program);
    sm = pio_claim_unused_sm(pio, true);
    silentype_sr_program_init(pio, sm, offset, PIN_SD, PIN_SC);
    sr_write(0);                                   // everything off, motors de-energized
}

uint16_t sr_make(uint8_t head7, uint8_t carr4, uint8_t feed4) {
    return (uint16_t)(((head7 & 0x7Fu) << SR_HEAD_SHIFT) | ((feed4 & 0xFu) << SR_FEED_SHIFT) | (carr4 & 0xFu));
}

void sr_write(uint16_t word) {
    pio_interrupt_clear(pio, 0);
    pio_sm_put_blocking(pio, sm, word);
    while (!pio_interrupt_get(pio, 0)) tight_loop_contents();
    pio_interrupt_clear(pio, 0);
    // Store clock: 74LS673 latches on the ST CK edge. The printer's 555 is triggered via SC·XC.
    gpio_put(PIN_XC, 1);
    busy_wait_us(2);
    gpio_put(PIN_XC, 0);
}
