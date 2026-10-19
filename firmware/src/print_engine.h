#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "protocol.h"
enum engine_err { ERR_NONE = 0, ERR_CRC, ERR_HEAD_FAULT, ERR_HOME_TIMEOUT, ERR_PAPER_OUT, ERR_INVALID_BAND };
typedef enum { QUEUE_OK = 0, QUEUE_FULL, QUEUE_INVALID, QUEUE_ERRSTATE } queue_result_t;
void           engine_init(void);
queue_result_t engine_queue_band(const uint8_t *band_payload, uint16_t len);
void           engine_request_reset(void);       // abort + clear + rehome (async)
void           engine_request_feed(uint16_t steps);
void           engine_request_selftest(void);
void           engine_set_error(uint8_t err);    // e.g. ERR_CRC from parser
bool           engine_set_param(uint8_t id, uint32_t value);   // bench tuning, RAM only
void           engine_fill_status(status_t *s);
