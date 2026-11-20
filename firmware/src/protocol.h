// protocol.h — wire format (docs/protocol.md). Shared by firmware and host. LE, packed.
#pragma once
#include <stdint.h>
#define PROTO_MAGIC 0xA5
enum proto_cmd { CMD_BAND = 0x01, CMD_FEED = 0x02, CMD_STATUS = 0x03, CMD_RESET = 0x04,
                 CMD_SETPARAM = 0x05, CMD_SELFTEST = 0x06,
                 RSP_STATUS = 0x83, RSP_ACK = 0x8F };
enum setparam_id {
    PARAM_BURN_US_1 = 0x01,   // 0x01..0x08 = burn µs for intensity LEVELS 0..7: id = 0x01 + level
    PARAM_STEP_INTERVAL_US = 0x10, PARAM_COOLDOWN_US = 0x11,
    PARAM_REVERSE_OFFSET = 0x12, PARAM_FEED_STEPS = 0x13,
    PARAM_HEAT_DECAY = 0x20, PARAM_HEAT_K = 0x21,
    PARAM_SAVE_NOT_SUPPORTED = 0xFF,
};
// SET_PARAM id for the burn time of intensity level lvl (0..MAX_INTENSITY). Note the
// +1: id 0x01 is level 0, not level 1.
#define PARAM_BURN_US_LVL(lvl) (PARAM_BURN_US_1 + (lvl))
enum ack_result { ACK_OK = 0, ACK_CRC = 1, ACK_INVALID = 2, ACK_ERRSTATE = 3 };
#pragma pack(push, 1)
typedef struct { uint8_t magic, cmd; uint16_t len; } proto_hdr_t;   // + payload + crc8
// BAND header, followed by width_cols column bytes. Column byte: bit 6 = TOP dot,
// bit 0 = BOTTOM dot (bit i drives head latch output Y(9+i)), bit 7 unused.
// intensity 0..MAX_INTENSITY (>MAX → DEFAULT_INTENSITY); feed_steps 0 = FEED_STEPS_PER_BAND.
typedef struct { uint16_t width_cols; uint8_t intensity, feed_steps, flags, reserved; } band_hdr_t;
typedef struct {
    uint8_t  state, error_code, buffer_bands, buffer_max;
    uint32_t bands_printed, uptime_ms;
    uint16_t head_mA_last, reserved;
} status_t;
#pragma pack(pop)
uint8_t crc8_07(const uint8_t *data, uint32_t len);
