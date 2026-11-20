// shiftreg.c — the printer's 74LS673, driven entirely by one PIO state machine.
//
// One sr_write() = one atomic PIO transaction: 16 bits MSB-first on SD/SC, then the store
// (SC low → XC pulse → SC high), then IRQ 0. The CPU only hands over the word and waits for
// the IRQ, so the SD/SC/XC edge order cannot be disturbed by interrupts or the other core.
// Waveform, timing and the evidence for every edge: silentype_sr.pio header and
// docs/firmware-archaeology/HARDWARE-INTERFACE.md §2.
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "config.h"
#include "shiftreg.h"
#include "silentype_sr.pio.h"

_Static_assert(PIN_XC == PIN_SC + 1, "XC must be the GPIO after SC: they share one 2-pin PIO side-set");

#define SR_PIO_HZ      2000000u          // 0.5 µs per PIO cycle → SC low 1 µs, XC high 2 µs
#define SR_DONE_IRQ    0u                // PIO IRQ flag raised after the store

static PIO  pio = pio0;
static uint sm, offset;

void sr_init(void) {
    offset = pio_add_program(pio, &silentype_sr_program);
    sm = pio_claim_unused_sm(pio, true);
    pio_interrupt_clear(pio, SR_DONE_IRQ);
    // Holds SC=XC=1 for ~10 µs first: the printer's ST CLR̅ = NAND(SC,XC)·555 clears the
    // storage register (whatever it powered up with, incl. Y08=R/W̅) before anything is stored.
    silentype_sr_program_init(pio, sm, offset, PIN_SD, PIN_SC,
                              (float)clock_get_hz(clk_sys) / (float)SR_PIO_HZ);
    sr_write(0);                                   // then latch an explicit all-off word
}

uint16_t sr_make(uint8_t head7, uint8_t carr4, uint8_t feed4) {
    return (uint16_t)(((head7 & 0x7Fu) << SR_HEAD_SHIFT) | ((feed4 & 0xFu) << SR_FEED_SHIFT) |
                      ((carr4 & 0xFu) << SR_CARR_SHIFT));        // bit SR_RW_BIT (Y08) always 0
}

void sr_write(uint16_t word) {
    word &= (uint16_t)~(1u << SR_RW_BIT);          // Y08 drives the 673's own R/W̅: never store a 1
    pio_interrupt_clear(pio, SR_DONE_IRQ);
    // OUT shifts left, so OSR[31] (= word bit 15) is shifted first and lands at Y15.
    pio_sm_put_blocking(pio, sm, (uint32_t)word << 16);
    while (!pio_interrupt_get(pio, SR_DONE_IRQ)) tight_loop_contents();
    pio_interrupt_clear(pio, SR_DONE_IRQ);
    // Returns with the bus idle (SC=1 XC=0 SD=0) and the new outputs latched ~4.5 µs ago.
}
