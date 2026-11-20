// shiftreg.h — the ONLY path to the printer's 16-bit latch (74LS673 inside the printer).
// Word layout (config.h SR_*): bit N of the word = latch output YN.
//   [15..9] head elements Y15 (top dot) … Y09 (bottom dot)   — head bit i → Y(9+i)
//   [8]     Y08 = the 673's own R/W̅ pin: always 0 (sr_write forces it)
//   [7..4]  paper-motor phases Y07..Y04
//   [3..0]  carriage-motor phases Y03..Y00
// On the wire bit 15 is shifted first (first-in ends at Y15), as in Apple's card ROM.
#pragma once
#include <stdint.h>

// Claims a PIO SM, briefly asserts the printer's storage-register clear (SC=XC=1), then
// latches sr_make(0,0,0). Bus idle state afterwards and between writes: SC=1, XC=0, SD=0.
void     sr_init(void);

// Shift 16 bits and store them; outputs change only at the store (XC rising edge).
// Blocks ≈48 µs: the latch edge is ≈43 µs after the call and the call returns ≈4.5 µs after
// it, a fixed latency, so the interval between two sr_write() returns = time the first word
// was on the outputs. NOTE: the printer's 555 clears ALL outputs (motors too) if any head
// element stays latched on continuously for ≈5-6 ms; every burn must end with a head-off word.
void     sr_write(uint16_t word);

uint16_t sr_make(uint8_t head7, uint8_t carr4, uint8_t feed4);
