# Host software

[README](../README.md) · [Design overview](design-overview.md) · [Printer interface](printer-interface.md) · [Firmware design](firmware-design.md) · **Host software** · [Carrier board](../hardware/pcb/README.md) · [Bring-up and tests](bring-up.md)

The host sends 7-dot-tall bands to the Pico over USB serial, framed and CRC-checked, and the Pico
acknowledges each one. The wire format is specified in [`protocol.md`](protocol.md); this document
covers the transport choice, flow control and the two host programs.

## Transport

| OS | Device | Notes |
|---|---|---|
| Linux | `/dev/ttyACM*`, plus `/dev/silentype` with the udev rule | Serial group (`dialout`; `uucp` on Arch) |
| macOS | `/dev/cu.usbmodem*` | Use `cu.`, not `tty.` |

USB CDC-ACM needs no driver anywhere. USB printer class (class 7) was rejected: macOS claims it
with its own printer driver and Linux creates a phantom raw queue, both fighting the printer app.
The firmware services the port only while DTR is asserted (pyserial and `open()` both do).

## ACKs and flow control

![One page printed: ACKs, the queue and back-pressure](images/ack-sequence.png)

The host sends one command and waits for its reply. An ACK means **queued, not printed**, so the
host can run up to 8 entries ahead of the paper; once the queue is full the Pico stops reading
USB and the next ACK waits until a slot frees. Any host must allow an ACK wait longer than one
band's print time (the PAPPL app allows 10 s, `send_band.py` 15 s), and must not send RESET right
after a job's last ACK, because up to 8 bands and the page feed may still be printing.

## PAPPL printer app (`host/pappl-app/`)

A local IPP Everywhere server, so macOS and Linux add the Silentype from the ordinary print dialog.
Builds against libpappl 1.4.12 with no warnings, and its job and device code pass a fake-Pico
test. Run under a real PAPPL server, it added the printer and printed a PNG end to end to a fake
Pico. Not yet tried from a desktop print dialog or with a real printer.

```
make -C host/pappl-app            # needs PAPPL >= 1.4 (Arch: pacman -S pappl)
make -C host/pappl-app test       # fake Pico on a pty; no printer needed
./silentype-app server -o listen-hostname=localhost
./silentype-app add -d silentype -v serial:///dev/ttyACM0 -m silentype
```

- **Device scheme.** Its own `serial://` scheme talks to the port directly (no libusb). Frames
  are written straight to the port, never through PAPPL's write buffer, so each one reaches the
  Pico before the app waits for its reply. Replies are read with a deadline and resynchronise on
  noise or a bad CRC. Listing only probes ports whose USB ID is the Silentype-Pico's (1209:0001,
  read from sysfs on Linux) and reports those that answer STATUS.
- **Driver.** 1-bit black raster (PAPPL dithers); 75 × 40 dpi (≈ 560 columns over 7.5 in; one
  raster row per paper step); 8.5 × 11 in roll media with side margins that leave exactly 560
  columns, centred; print-darkness → intensity 0–7, default 1 (`printer-darkness-configured` 14).
- **Job flow.** At job start the app polls STATUS until the printer is idle. A previous job's
  bands and page feed may still be printing, so it never resets a busy printer; it sends RESET
  only to clear an error, then waits for the re-home. Every 7 raster lines become one BAND (line
  r → bit 6 − r), sent and ACK-checked. At page end the partial band is padded white, then FEED 40
  (≈ 1 inch). Every ACK is checked for the right command echo and an OK result.
- **Status.** STATUS maps the engine's error state to IPP printer-state-reasons (paper-out →
  media-empty, anything else → other).

## Bench tool (`host/tools/send_band.py`)

| Command | Example | Does |
|---|---|---|
| `status` | `send_band.py status` | State, error, queue use, bands printed, uptime |
| `reset` | `send_band.py reset` | Clear error and queue, re-home |
| `selftest` | `send_band.py selftest` | Built-in checker band, forward then reverse |
| `pattern` | `send_band.py pattern --intensity 0` | 200-column checker band |
| `image` | `send_band.py image chart.png --intensity 1` | Scale, pad to whole bands, threshold, send |
| `feed` | `send_band.py feed 40` | Paper feed in steps (40 ≈ 1 inch) |
| `burn` | `send_band.py burn 1 300` | Level 1 burn = 300 µs (SET_PARAM 0x02) |
| `setparam` | `send_band.py setparam 0x10 1500` | Raw SET_PARAM |

Needs pyserial (and Pillow for `image`); `--help` works without them. Replies are CRC-checked;
a missing ACK, wrong echo or NAK is reported and the tool exits 1.

## Installing

**Linux** (`host/linux/`): copy `99-silentype.rules` to `/etc/udev/rules.d/` (adjust the group on
Arch); install the app to `/usr/local/bin/`; create a `silentype` user in the serial group and
`/var/spool/silentype`; install `silentype-app.service` and `systemctl enable --now silentype-app`.

**macOS** (`host/macos/`): build PAPPL from source against Homebrew libcups; copy
`com.janus.silentype-app.plist` to `~/Library/LaunchAgents/` and `launchctl load` it.

## Open issues

| Severity | Issue | Next step |
|---|---|---|
| Medium | PNG/JPEG sent straight to the app print squeezed sideways: PAPPL's image scaler assumes square pixels, and the printer is 75 × 40 dpi | Advertise 75 × 75 dpi and merge rows 75 → 40 in the driver, or accept it (PDFs rendered by CUPS at the printer's resolution are not affected; to verify at M7) |
| Medium | Not yet tried from a desktop print dialog | Add the printer in macOS / GNOME and print a PDF (M7) |
| Medium | 75 × 40 dpi and 560 columns are estimates | Ruler check on a real print (M1); change `ST_XDPI`, `ST_MAX_COLS` |
| Low | Listing probes every port on macOS (no sysfs USB ID check there) | Match the USB ID through IOKit, or rely on the device URI |

Fixed in v0.7: frames were left in PAPPL's write buffer and never sent; the device-list callback
had the wrong return type and inverted stop logic; RESET at job start discarded the previous
job's queued bands; wrong media units, dpi and column count; darkness range; a non-existent
raster-type constant (`PAPPL_RASTER_TYPE_BLACK_1`); a raw `format` with no print-file callback;
replies split across USB packets read as a time-out; `ppm` 0, which PAPPL rejects when adding
the printer; no `force_raster_type`, so PAPPL sent 8-bit lines and every page printed blank. See [`KNOWN-ISSUES.md`](KNOWN-ISSUES.md).
