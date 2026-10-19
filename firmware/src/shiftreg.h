// shiftreg.h — the ONLY path to the printer's 16-bit latch.
// Word layout (config.h SR_*): [15..9]=head Y15..Y09, [8]=R/W=0, [7..4]=paper, [3..0]=carriage.
#pragma once
#include <stdint.h>
void     sr_init(void);
void     sr_write(uint16_t word);                 // shift 16 bits, pulse XC; blocks ~20 µs
uint16_t sr_make(uint8_t head7, uint8_t carr4, uint8_t feed4);
