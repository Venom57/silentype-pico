// tusb.h — host stub: CDC rx/tx are byte buffers driven by the test.
#pragma once
#include <stdint.h>
#include <stdbool.h>
void     tusb_init(void);
void     tud_task(void);
bool     tud_cdc_connected(void);
uint32_t tud_cdc_available(void);
uint32_t tud_cdc_read(void *buf, uint32_t n);
uint32_t tud_cdc_write(const void *buf, uint32_t n);
uint32_t tud_cdc_write_flush(void);

extern uint8_t  cdc_rx[], cdc_tx[];
extern uint32_t cdc_rx_len, cdc_rx_pos, cdc_tx_len;
