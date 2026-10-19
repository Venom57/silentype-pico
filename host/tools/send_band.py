#!/usr/bin/env python3
"""send_band.py — bench tool over CDC serial; no CUPS, no libusb permissions.

  ./send_band.py -p /dev/ttyACM0 selftest
  ./send_band.py -p /dev/cu.usbmodem1101 status
  ./send_band.py pattern --intensity 2            # auto-detects port if one candidate
  ./send_band.py image photo.png --intensity 3
  ./send_band.py setparam 0x01 300          # BURN_US[1]=300 µs (RAM only; ids in protocol.md)
  ./send_band.py feed 20

pip install pyserial pillow
"""
import argparse, glob, struct, sys, time

HEAD_DOTS = 7        # TODO(M1): sync with firmware config.h — UNVERIFIED
MAX_COLS  = 480      # TODO(M0): sync with firmware config.h
CMD = dict(BAND=1, FEED=2, STATUS=3, RESET=4, SETPARAM=5, SELFTEST=6)
RSP_STATUS, RSP_ACK = 0x83, 0x8F
ERR = ["none", "crc", "head_fault", "home_timeout", "paper_out", "invalid_band"]

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

def band(cols: bytes, intensity=1, feed=0, flags=0) -> bytes:
    return frame(CMD["BAND"], struct.pack("<HBBBB", len(cols), intensity, feed, flags, 0) + cols)

def checker(width=200) -> bytes:
    return bytes((0x55 if i % 2 else 0x2A) for i in range(width))

def image_bands(path: str, intensity: int):
    """Resize to printable width (no stretch), pad height to a band multiple, then threshold."""
    from PIL import Image
    img = Image.open(path).convert("L")
    if img.width > MAX_COLS:
        img = img.resize((MAX_COLS, max(1, round(img.height * MAX_COLS / img.width))), Image.LANCZOS)
    pad_h = (-img.height) % HEAD_DOTS
    if pad_h:
        canvas = Image.new("L", (img.width, img.height + pad_h), 255)
        canvas.paste(img, (0, 0)); img = canvas
    px = img.load()
    for top in range(0, img.height, HEAD_DOTS):
        cols = bytearray(img.width)
        for x in range(img.width):
            v = 0
            for bit in range(HEAD_DOTS):
                if px[x, top + bit] < 128:            # dark pixel -> burn
                    v |= 1 << bit
            cols[x] = v
        yield band(bytes(cols), intensity=intensity)

def read_frame(ser, timeout=2.0):
    end = time.time() + timeout
    buf = b""
    while time.time() < end:
        buf += ser.read(ser.in_waiting or 1)
        while buf and buf[0] != 0xA5: buf = buf[1:]
        if len(buf) >= 4:
            ln = struct.unpack_from("<H", buf, 2)[0]
            if len(buf) >= 4 + ln + 1:
                cmd, payload = buf[1], buf[4:4 + ln]
                return cmd, payload
    return None, None

def send(ser, f, expect_ack=True):
    ser.write(f)
    if expect_ack:
        cmd, p = read_frame(ser)
        if cmd == RSP_ACK and p[1] != 0:
            print(f"NAK for cmd {p[0]:#x}: result {p[1]}", file=sys.stderr)

def find_port():
    c = glob.glob("/dev/ttyACM*") + glob.glob("/dev/cu.usbmodem*")
    if len(c) != 1: sys.exit(f"specify -p; candidates: {c}")
    return c[0]

def main():
    import serial
    ap = argparse.ArgumentParser()
    ap.add_argument("-p", "--port")
    ap.add_argument("action", choices=["selftest", "status", "reset", "pattern", "image", "setparam", "feed"])
    ap.add_argument("arg2", nargs="?")
    ap.add_argument("arg", nargs="?")
    ap.add_argument("--intensity", type=int, default=1)
    a = ap.parse_args()
    ser = serial.Serial(a.port or find_port(), 115200, timeout=0.1, write_timeout=30)

    if a.action == "selftest": send(ser, frame(CMD["SELFTEST"]))
    elif a.action == "reset":  send(ser, frame(CMD["RESET"]))
    elif a.action == "status":
        ser.write(frame(CMD["STATUS"])); cmd, p = read_frame(ser)
        if cmd != RSP_STATUS: sys.exit("no status reply")
        st, err, nb, mx, printed, up, mA, _ = struct.unpack("<BBBBIIHH", p)
        print(f"state={['idle','printing','error'][st]} err={ERR[err]} queue={nb}/{mx} printed={printed} up={up}ms head={mA}mA")
    elif a.action == "pattern": send(ser, band(checker(), intensity=a.intensity))
    elif a.action == "image":
        for f in image_bands(a.arg, a.intensity): send(ser, f)
    elif a.action == "feed":
        send(ser, frame(CMD["FEED"], struct.pack("<H", int(a.arg))))
    elif a.action == "setparam":   # setparam <id> <value>   e.g. setparam 0x01 300  (BURN_US[1])
        pid, val = int(a.arg, 0), int(a.arg2, 0) & 0xFFFFFFFF
        send(ser, frame(CMD["SETPARAM"], struct.pack("<BI", pid, val)))

if __name__ == "__main__":
    main()
