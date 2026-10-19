// main.c — core0: TinyUSB CDC + frame parser. Mechanism work is on core1.
//
// Flow control: when the band queue is full we simply stop reading the CDC FIFO.
// TinyUSB NAKs the host once its RX buffer fills; the host's write() blocks.

#include <string.h>
#include "pico/stdlib.h"
#include "tusb.h"
#include "config.h"
#include "protocol.h"
#include "print_engine.h"

#define MAX_FRAME (sizeof(proto_hdr_t) + sizeof(band_hdr_t) + MAX_COLS + 1)
static uint8_t  rx[MAX_FRAME];
static uint32_t rxlen;

static void send_frame(uint8_t cmd, const void *payload, uint16_t len) {
    uint8_t buf[sizeof(proto_hdr_t) + 32 + 1];
    if (len > 32) return;
    proto_hdr_t *h = (proto_hdr_t *)buf;
    h->magic = PROTO_MAGIC; h->cmd = cmd; h->len = len;
    memcpy(buf + sizeof *h, payload, len);
    buf[sizeof *h + len] = crc8_07(buf + 1, 3 + len);           // cmd..payload
    tud_cdc_write(buf, sizeof *h + len + 1);
    tud_cdc_write_flush();
}
static void send_ack(uint8_t cmd, uint8_t result) { uint8_t p[2] = { cmd, result }; send_frame(RSP_ACK, p, 2); }

// Returns true if the frame was consumed (ACK/NAK sent); false = retry later (queue full).
static bool handle_frame(const proto_hdr_t *h, const uint8_t *payload) {
    status_t st;
    switch (h->cmd) {
    case CMD_BAND:
        switch (engine_queue_band(payload, h->len)) {
        case QUEUE_OK:       send_ack(h->cmd, ACK_OK);       return true;
        case QUEUE_INVALID:  send_ack(h->cmd, ACK_INVALID);  return true;  // consumed, not spun on
        case QUEUE_ERRSTATE: send_ack(h->cmd, ACK_ERRSTATE); return true;
        case QUEUE_FULL:     return false;                                 // back-pressure
        }
        return true;
    case CMD_FEED:
        if (h->len != 2) { send_ack(h->cmd, ACK_INVALID); return true; }
        engine_request_feed((uint16_t)(payload[0] | (payload[1] << 8)));
        send_ack(h->cmd, ACK_OK); return true;
    case CMD_STATUS:   engine_fill_status(&st); send_frame(RSP_STATUS, &st, sizeof st); return true;
    case CMD_RESET:    engine_request_reset();    send_ack(h->cmd, ACK_OK); return true;
    case CMD_SELFTEST: engine_request_selftest(); send_ack(h->cmd, ACK_OK); return true;
    case CMD_SETPARAM:
        if (h->len != 5) { send_ack(h->cmd, ACK_INVALID); return true; }
        send_ack(h->cmd, engine_set_param(payload[0],
                 (uint32_t)payload[1] | (uint32_t)payload[2] << 8 | (uint32_t)payload[3] << 16 | (uint32_t)payload[4] << 24)
                 ? ACK_OK : ACK_INVALID);
        return true;
    default:           send_ack(h->cmd, ACK_INVALID); return true;
    }
}

// Pull bytes from CDC, resync on magic, validate, dispatch.
static void pump_rx(void) {
    for (;;) {
        // 1. resync: drop bytes until magic is at rx[0]
        while (rxlen && rx[0] != PROTO_MAGIC) { memmove(rx, rx + 1, --rxlen); }
        // 2. need header
        if (rxlen < sizeof(proto_hdr_t)) goto fill;
        const proto_hdr_t *h = (const proto_hdr_t *)rx;
        uint32_t need = sizeof *h + h->len + 1;
        if (need > MAX_FRAME) { memmove(rx, rx + 1, --rxlen); continue; }   // bogus len: resync
        if (rxlen < need) goto fill;
        // 3. crc
        if (crc8_07(rx + 1, 3 + h->len) != rx[need - 1]) {
            engine_set_error(ERR_CRC); send_ack(h->cmd, ACK_CRC);
            memmove(rx, rx + 1, --rxlen); continue;                          // resync past magic
        }
        // 4. dispatch; if back-pressured, leave the frame in place and stop reading
        if (!handle_frame(h, rx + sizeof *h)) return;
        memmove(rx, rx + need, rxlen - need); rxlen -= need;
        continue;
    fill:
        if (!tud_cdc_available()) return;
        rxlen += tud_cdc_read(rx + rxlen, sizeof rx - rxlen);
    }
}

int main(void) {
    stdio_uart_init();
    engine_init();                 // launches core1; head outputs low; strobe low
    tusb_init();
    for (;;) { tud_task(); if (tud_cdc_connected()) pump_rx(); }
}
