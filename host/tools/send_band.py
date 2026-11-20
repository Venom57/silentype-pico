#!/usr/bin/env python3
"""send_band.py — bench tool over CDC serial; no CUPS, no libusb permissions.

  ./send_band.py -p /dev/ttyACM0 selftest
  ./send_band.py -p /dev/cu.usbmodem1101 status
  ./send_band.py pattern --intensity 2            # auto-detects port if one candidate
  ./send_band.py image photo.png --intensity 3
  ./send_band.py burn 1 300                       # burn time for intensity level 1 = 300 µs (SET_PARAM id 0x02)
  ./send_band.py setparam 0x10 1500               # raw SET_PARAM <id> <value> (RAM only; ids in protocol.md)
  ./send_band.py feed 20
  ./send_band.py reset

Band column bytes: bit 6 = TOP dot, bit 0 = BOTTOM dot, bit 7 unused (docs/protocol.md).

pip install pyserial pillow     (pillow only needed for `image`)
"""
import argparse, glob, struct, sys, time

HEAD_DOTS = 7        # ROM-confirmed (docs/firmware-archaeology)
MAX_COLS  = 480      # TODO(M0): sync with firmware config.h
MAX_INTENSITY = 7    # firmware config.h; levels 0..7, >7 → firmware DEFAULT_INTENSITY
DEFAULT_INTENSITY = 1
CMD = dict(BAND=1, FEED=2, STATUS=3, RESET=4, SETPARAM=5, SELFTEST=6)
RSP_STATUS, RSP_ACK = 0x83, 0x8F
PARAM_BURN_US_1 = 0x01                       # SET_PARAM id = PARAM_BURN_US_1 + level
ERR = ["none", "crc", "head_fault", "home_timeout", "paper_out", "invalid_band"]
ACK_RESULT = ["ok", "crc", "invalid", "error-state"]
MAX_REPLY_LEN = 64   # replies are ACK (2) or STATUS (16); anything longer is line noise

def crc8(data: bytes) -> int:
    c = 0
    for b in data:
        c ^= b
        for _ in range(8):
            c = ((c << 1) ^ 0x07) & 0xFF if c & 0x80 else (c << 1) & 0xFF
    return c

def frame(cmd: int, payload: bytes = b"") -> bytes:
    body = bytes([cmd]) + struct.pack("<H", len(payload)) + payload
    return b"\xA5" + body + bytes([crc8(body)])

def band(cols: bytes, intensity=DEFAULT_INTENSITY, feed=0, flags=0) -> bytes:
    """feed=0 → firmware default FEED_STEPS_PER_BAND."""
    return frame(CMD["BAND"], struct.pack("<HBBBB", len(cols), intensity, feed, flags, 0) + cols)

def checker(width=200) -> bytes:
    # Same as the firmware self-test: alternating 0x2A/0x55 gives a checkerboard.
    return bytes((0x55 if i % 2 else 0x2A) for i in range(width))

def pack_column(dark) -> int:
    """dark[r] truthy for raster row r of the band (0 = top). Row r → bit HEAD_DOTS-1-r,
    so the top row lands in bit 6 (top head element) and the bottom row in bit 0."""
    v = 0
    for r in range(HEAD_DOTS):
        if dark[r]:
            v |= 1 << (HEAD_DOTS - 1 - r)
    return v

def image_bands(path_or_img, intensity: int):
    """Resize to printable width (no stretch), pad height to a band multiple with white, then threshold.
    Accepts a path or an already-open PIL-like image."""
    from PIL import Image
    img = path_or_img if hasattr(path_or_img, "convert") else Image.open(path_or_img)
    img = img.convert("L")
    if img.width > MAX_COLS:
        img = img.resize((MAX_COLS, max(1, round(img.height * MAX_COLS / img.width))), Image.LANCZOS)
    pad_h = (-img.height) % HEAD_DOTS
    if pad_h:
        canvas = Image.new("L", (img.width, img.height + pad_h), 255)   # white padding → blank dots
        canvas.paste(img, (0, 0)); img = canvas
    px = img.load()
    for top in range(0, img.height, HEAD_DOTS):
        cols = bytes(pack_column([px[x, top + r] < 128 for r in range(HEAD_DOTS)])   # dark pixel → burn
                     for x in range(img.width))
        yield band(cols, intensity=intensity)

def read_frame(ser, timeout=2.0):
    """Return (cmd, payload) of the next CRC-valid frame, or (None, None) on timeout."""
    end = time.time() + timeout
    buf = b""
    while time.time() < end:
        buf += ser.read(ser.in_waiting or 1)
        while True:
            i = buf.find(b"\xA5")
            if i < 0: buf = b""; break
            buf = buf[i:]
            if len(buf) < 4: break
            ln = struct.unpack_from("<H", buf, 2)[0]
            if ln > MAX_REPLY_LEN: buf = buf[1:]; continue            # not a real header; resync
            if len(buf) < 4 + ln + 1: break
            if crc8(buf[1:4 + ln]) != buf[4 + ln]: buf = buf[1:]; continue   # bad CRC; resync
            return buf[1], bytes(buf[4:4 + ln])
    return None, None

def send(ser, f, expect_ack=True, timeout=15.0) -> bool:
    """Write a frame; with expect_ack, wait for its ACK and report NAK/timeout. Returns True on ACK_OK.
    BAND ACKs are held back while the firmware queue is full, so the timeout must cover a band print."""
    ser.write(f)
    if not expect_ack:
        return True
    cmd, p = read_frame(ser, timeout)
    if cmd is None:
        print(f"no ACK for cmd {f[1]:#04x} within {timeout:g}s", file=sys.stderr); return False
    if cmd != RSP_ACK or len(p) < 2:
        print(f"unexpected reply {cmd:#04x} ({p.hex()}) to cmd {f[1]:#04x}", file=sys.stderr); return False
    if p[0] != f[1]:
        print(f"ACK echoes cmd {p[0]:#04x}, expected {f[1]:#04x}", file=sys.stderr); return False
    if p[1] != 0:
        name = ACK_RESULT[p[1]] if p[1] < len(ACK_RESULT) else "?"
        print(f"NAK for cmd {p[0]:#04x}: result {p[1]} ({name})", file=sys.stderr); return False
    return True

def find_port():
    c = glob.glob("/dev/ttyACM*") + glob.glob("/dev/cu.usbmodem*")
    if len(c) != 1: sys.exit(f"specify -p; candidates: {c}")
    return c[0]

def int_auto(s: str) -> int:
    return int(s, 0)        # accepts 0x10, 16, 0o20, -5

def intensity_arg(s: str) -> int:
    v = int(s, 0)
    if not 0 <= v <= MAX_INTENSITY:
        raise argparse.ArgumentTypeError(f"{v} not in 0..{MAX_INTENSITY}")
    return v

def build_parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter,
                                 epilog=__doc__.split("\n", 1)[1])
    ap.add_argument("-p", "--port", help="serial port (default: auto-detect a single /dev/ttyACM* or /dev/cu.usbmodem*)")
    sub = ap.add_subparsers(dest="action", required=True, metavar="ACTION")
    sub.add_parser("selftest", help="print the firmware's built-in pattern")
    sub.add_parser("status", help="query and print printer status")
    sub.add_parser("reset", help="abort, clear queue and error, home")
    s = sub.add_parser("pattern", help="print one checkerboard band")
    s.add_argument("--intensity", type=intensity_arg, default=DEFAULT_INTENSITY, help=f"0..{MAX_INTENSITY} (default %(default)s)")
    s = sub.add_parser("image", help="print an image file (needs Pillow)")
    s.add_argument("path", metavar="PATH")
    s.add_argument("--intensity", type=intensity_arg, default=DEFAULT_INTENSITY, help=f"0..{MAX_INTENSITY} (default %(default)s)")
    s = sub.add_parser("feed", help="advance paper")
    s.add_argument("steps", metavar="STEPS", type=int_auto)
    s = sub.add_parser("burn", help="set burn time for one intensity level (SET_PARAM id 0x01+LEVEL)")
    s.add_argument("level", metavar="LEVEL", type=intensity_arg, help=f"intensity level 0..{MAX_INTENSITY}")
    s.add_argument("us", metavar="MICROSECONDS", type=int_auto)
    s = sub.add_parser("setparam", help="raw SET_PARAM (ids in docs/protocol.md)")
    s.add_argument("id", metavar="ID", type=int_auto)
    s.add_argument("value", metavar="VALUE", type=int_auto)
    return ap

def main(argv=None):
    a = build_parser().parse_args(argv)
    if a.action == "feed" and not 0 <= a.steps <= 0xFFFF: sys.exit("feed: STEPS must be 0..65535")
    if a.action == "setparam" and not 0 <= a.id <= 0xFF: sys.exit("setparam: ID must be 0..0xFF")
    if a.action == "burn" and a.us <= 0: sys.exit("burn: MICROSECONDS must be > 0")

    import serial
    ser = serial.Serial(a.port or find_port(), 115200, timeout=0.1, write_timeout=30)
    ser.reset_input_buffer()
    ok = True

    if a.action == "selftest": ok = send(ser, frame(CMD["SELFTEST"]))
    elif a.action == "reset":  ok = send(ser, frame(CMD["RESET"]))
    elif a.action == "status":
        ser.write(frame(CMD["STATUS"])); cmd, p = read_frame(ser)
        if cmd != RSP_STATUS or len(p) != 16: sys.exit("no status reply")
        st, err, nb, mx, printed, up, mA, _ = struct.unpack("<BBBBIIHH", p)
        state = ['idle', 'printing', 'error'][st] if st < 3 else st
        print(f"state={state} err={ERR[err] if err < len(ERR) else err} queue={nb}/{mx} printed={printed} up={up}ms head={mA}mA")
    elif a.action == "pattern": ok = send(ser, band(checker(), intensity=a.intensity))
    elif a.action == "image":
        for n, f in enumerate(image_bands(a.path, a.intensity)):
            if not send(ser, f):
                print(f"stopped at band {n}", file=sys.stderr); ok = False; break
    elif a.action == "feed":
        ok = send(ser, frame(CMD["FEED"], struct.pack("<H", a.steps)))
    elif a.action == "burn":
        ok = send(ser, frame(CMD["SETPARAM"], struct.pack("<BI", PARAM_BURN_US_1 + a.level, a.us)))
    elif a.action == "setparam":
        ok = send(ser, frame(CMD["SETPARAM"], struct.pack("<BI", a.id, a.value & 0xFFFFFFFF)))
    sys.exit(0 if ok else 1)

if __name__ == "__main__":
    main()
