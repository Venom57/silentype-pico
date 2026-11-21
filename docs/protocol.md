# Band Protocol (host ⇄ Pico) over USB CDC serial

[README](../README.md) · [Design overview](../docs/design-overview.md) · [Printer interface](../docs/printer-interface.md) · [Firmware design](../docs/firmware-design.md) · [Host software](../docs/host-software.md) · [Carrier board](../hardware/pcb/README.md) · [Bring-up and tests](../docs/bring-up.md)

Transport choice, flow control and the host programs: [host software](host-software.md).

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
| 0x02 | FEED | `u16 steps` | Paper feed, queued in order with bands (0 = no-op). Back-pressured like BAND |
| 0x03 | STATUS | none | Reply with STATUS frame |
| 0x04 | RESET | none | Abort, clear queue, clear error, home |
| 0x05 | SET_PARAM | `u8 id, u32 value` | Bench tuning, RAM only, refused while printing. Burn values capped at BURN_HARD_LIMIT_US |
| 0x06 | SELFTEST | none | Print built-in pattern |

### BAND payload
```
u16 width_cols
u8  intensity      // 0..MAX_INTENSITY (= 7); values > 7 → firmware DEFAULT_INTENSITY
u8  feed_steps     // paper advance after band; 0 = firmware default FEED_STEPS_PER_BAND
u8  flags          // bit0: force forward pass (default: engine alternates direction)
u8  reserved
u8  cols[width_cols]   // one byte per column, left to right; see bit order below
```

Column byte bit order (HEAD_DOTS = 7):

| bit | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
|-----|---|---|---|---|---|---|---|---|
| dot | unused (send 0) | **top** | | | | | | **bottom** |

Bit i drives head latch output Y(9+i); per the printer schematic (J4) Y15 is the top
dot and Y09 the bottom, matching the Apple /// driver's font table (bit 6 = top row,
e.g. 'T' first column = 0x40). So raster row r of a band (r = 0 at the top) goes to
bit `HEAD_DOTS − 1 − r`. Rows padded past the end of a page/image are blank (0).

## Replies (Pico → host) — same frame format

| CMD | Payload |
|-----|---------|
| 0x83 | STATUS struct (below) |
| 0x8F | ACK/NAK: `u8 cmd_echo, u8 result` (0 ok, 1 crc, 2 invalid, 3 error-state) |

### STATUS (16 bytes)
```
u8  state        // 0 idle (homed, queue empty), 1 busy (printing, feeding or homing), 2 error
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
| 0x01–0x08 | burn µs for intensity level 0..7 (**id = 0x01 + level**: 0x01 = level 0, 0x08 = level 7) | 1..BURN_HARD_LIMIT_US |
| 0x10 | carriage step interval µs | 300..20000 |
| 0x11 | inter-column cooldown µs | 0..20000 |
| 0x12 | reverse-pass offset steps (signed) | -50..50 |
| 0x13 | default feed steps per band | 1..255 |
| 0x20 / 0x21 | heat decay / heat K | 0..255 (0 disables) |

Geometry (`MAX_COLS`, `HEAD_DOTS`, native DPI) comes from `firmware/src/config.h`.
Host code reads the same values — single source of truth.

## Example frames

```
STATUS request          a5 03 00 00 bd
RESET                   a5 04 00 00 ab
FEED 40 steps           a5 02 02 00 28 00 ee
BAND, 3 columns, lvl 1  a5 01 09 00 03 00 01 00 00 00 7f 41 7f fa
ACK for a BAND, OK      a5 8f 02 00 01 00 9e
```

CRC-8 is polynomial 0x07, initial value 0, no reflection, no final XOR (0xF4 for "123456789").
