#include "protocol.h"
uint8_t crc8_07(const uint8_t *d, uint32_t n) {
    uint8_t c = 0;
    while (n--) { c ^= *d++; for (int i = 0; i < 8; i++)
        c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0x07) : (uint8_t)(c << 1); }
    return c;
}
