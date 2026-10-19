# Band Protocol (host ⇄ Pico) over USB CDC serial

Transport: **USB CDC-ACM**. Enumerates as a serial port on macOS (`/dev/cu.usbmodem*`)
and Linux (`/dev/ttyACM*`) with no driver. Chosen over printer class 7 because PAPPL
owns the device: a class-7 interface gets claimed by the macOS printer class driver
and produces a phantom raw queue on Linux. Flow control: the Pico stops draining
its CDC FIFO when the band queue is full; the host's write blocks (USB NAK).
Baud rate is ignored (USB).

All multi-byte fields little-endian.

## Frame

```
+------+------+---------+----------------+-------+
| 0xA5 | CMD  | LEN u16 | PAYLOAD (LEN)  | CRC8  |
+------+------+---------+----------------+-------+
```
CRC8 poly 0x07 over CMD..PAYLOAD. Bad CRC ⇒ frame dropped, ERR_CRC latched in STATUS.
Parser resyncs by hunting for 0xA5.

## Commands (host → Pico)

| CMD | Name | Payload | Action |
|-----|------|---------|--------|
| 0x01 | BAND | header + column data | Queue one band |
| 0x02 | FEED | `u16 steps` | Paper feed only |
| 0x03 | STATUS | none | Reply with STATUS frame |
| 0x04 | RESET | none | Abort, clear queue, clear error, home |
| 0x05 | SET_PARAM | `u8 id, u32 value` | Bench tuning, RAM only, refused while printing. Burn values capped at BURN_HARD_LIMIT_US |
| 0x06 | SELFTEST | none | Print built-in pattern |

### BAND payload
```
u16 width_cols
u8  intensity      // 1..MAX_INTENSITY
u8  feed_steps     // paper advance after band; 0 = overprint
u8  flags          // bit0: force forward pass (default: engine alternates direction)
u8  reserved
u8  cols[width_cols]   // bit i = head element i. Element count = HEAD_DOTS
                        // (≤ 8 fits one byte; if M1 finds >8, this becomes u16)
```

## Replies (Pico → host) — same frame format

| CMD | Payload |
|-----|---------|
| 0x83 | STATUS struct (below) |
| 0x8F | ACK/NAK: `u8 cmd_echo, u8 result` (0 ok, 1 crc, 2 invalid, 3 error-state) |

### STATUS (16 bytes)
```
u8  state        // 0 idle, 1 printing, 2 error
u8  error_code   // 0 none, 1 crc, 2 head_fault, 3 home_timeout, 4 paper_out, 5 invalid_band
u8  buffer_bands
u8  buffer_max
u32 bands_printed
u32 uptime_ms
u16 head_mA_last // last measured head current (Option B), 0 if N/A
u16 reserved
```

### SET_PARAM ids
| id | Parameter | Range |
|----|-----------|-------|
| 0x01–0x05 | BURN_US[1..5] µs | 1..BURN_HARD_LIMIT_US |
| 0x10 | carriage step interval µs | 300..20000 |
| 0x11 | inter-column cooldown µs | 0..20000 |
| 0x12 | reverse-pass offset steps (signed) | -50..50 |
| 0x13 | default feed steps per band | 1..255 |
| 0x20 / 0x21 | heat decay / heat K | 0..255 (0 disables) |

Geometry (`MAX_COLS`, `HEAD_DOTS`, native DPI) comes from `firmware/src/config.h`.
Host code reads the same values — single source of truth.
