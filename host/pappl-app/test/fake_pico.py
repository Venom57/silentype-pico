#!/usr/bin/env python3
"""Fake Silentype-Pico on a pseudo-terminal, for testing silentype-app.c.

Runs test_app against each scenario and checks the frames the app sends:
command order, ACK handling, BAND contents and the wait-for-idle logic.
Usage: fake_pico.py ./test_app
"""
import os, pty, subprocess, sys, threading, time, tty

MAGIC = 0xA5
BAND, FEED, STATUS, RESET = 1, 2, 3, 4
RSP_STATUS, RSP_ACK = 0x83, 0x8F
IDLE, BUSY, ERROR = 0, 1, 2
MAX_COLS, HEAD_DOTS = 560, 7


def crc8(data):
    c = 0
    for b in data:
        c ^= b
        for _ in range(8):
            c = ((c << 1) ^ 0x07) & 0xFF if c & 0x80 else (c << 1) & 0xFF
    return c


def frame(cmd, payload=b""):
    body = bytes([cmd, len(payload) & 0xFF, len(payload) >> 8]) + payload
    return bytes([MAGIC]) + body + bytes([crc8(body)])


class FakePico:
    """statuses: list of (state, error) replies to successive STATUS requests (last repeats).
    after_reset: replaces the status list when a RESET arrives."""

    def __init__(self, statuses, after_reset=None, ack_delay=None, nak_band=None,
                 noise=False, split=False):
        self.statuses, self.after_reset = list(statuses), after_reset
        self.ack_delay, self.nak_band = ack_delay or {}, nak_band
        self.noise, self.split = noise, split
        self.frames, self.bands = [], 0
        self.master, slave = pty.openpty()
        tty.setraw(slave)
        self.slave = slave
        self.path = os.ttyname(slave)
        self.stop = False
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def send(self, data):
        if self.noise:
            os.write(self.master, b"\x00\x13garbage" + frame(RSP_ACK, b"\x01\x00")[:-1] + b"\x55")
        if self.split:
            for b in data:
                os.write(self.master, bytes([b])); time.sleep(0.002)
        else:
            os.write(self.master, data)

    def status_reply(self):
        state, err = self.statuses.pop(0) if len(self.statuses) > 1 else self.statuses[0]
        payload = bytes([state, err, 0, 8]) + bytes(12)
        self.send(frame(RSP_STATUS, payload))

    def handle(self, cmd, payload):
        self.frames.append((cmd, payload))
        if cmd == STATUS:
            self.status_reply(); return
        result = 0
        if cmd == BAND:
            self.bands += 1
            if self.bands == self.nak_band:
                result = 3
            time.sleep(self.ack_delay.get(self.bands, 0))
        if cmd == RESET and self.after_reset is not None:
            self.statuses = list(self.after_reset)
        self.send(frame(RSP_ACK, bytes([cmd, result])))

    def run(self):
        buf = b""
        while not self.stop:
            try:
                chunk = os.read(self.master, 4096)
            except OSError:
                return
            buf += chunk
            while True:
                i = buf.find(bytes([MAGIC]))
                if i < 0:
                    buf = b""; break
                buf = buf[i:]
                if len(buf) < 4:
                    break
                n = buf[2] | buf[3] << 8
                if len(buf) < 5 + n:
                    break
                body, crc = buf[1:4 + n], buf[4 + n]
                buf = buf[5 + n:]
                if crc8(body) != crc:
                    self.frames.append(("BADCRC", body)); continue
                self.handle(body[0], body[3:])


def check_band(payload, rows):
    width = payload[0] | payload[1] << 8
    assert width == MAX_COLS, f"band width {width}"
    assert payload[2] == 1, f"intensity {payload[2]}"
    assert payload[3] == 0 and payload[4] == 0, "feed/flags"
    cols = payload[6:]
    assert len(cols) == width
    for x, c in enumerate(cols):
        r = x % 8
        want = (1 << (HEAD_DOTS - 1 - r)) if r < rows else 0
        assert c == want, f"column {x}: {c:#04x}, want {want:#04x}"


NAMES = {BAND: "BAND", FEED: "FEED", STATUS: "STATUS", RESET: "RESET"}
PAGE = ["BAND", "BAND", "FEED"]          # 10 rows: one full band, one 3-row band, page feed

SCENARIOS = [
    # name, test_app arg, FakePico kwargs, expected rc, expected command list
    ("idle printer: no RESET", "page", dict(statuses=[(IDLE, 0)]), 0, ["STATUS"] + PAGE),
    ("busy printer: wait, no RESET", "page", dict(statuses=[(BUSY, 0)] * 3 + [(IDLE, 0)]), 0, ["STATUS"] * 4 + PAGE),
    ("error: RESET, wait for re-home", "page",
     dict(statuses=[(ERROR, 3)], after_reset=[(BUSY, 0), (BUSY, 0), (IDLE, 0)]), 0,
     ["STATUS", "RESET", "STATUS", "STATUS", "STATUS"] + PAGE),
    ("error persists after RESET: job fails", "page",
     dict(statuses=[(ERROR, 3)], after_reset=[(BUSY, 0), (ERROR, 3)]), 2,
     ["STATUS", "RESET", "STATUS", "STATUS"]),
    ("back-pressure: slow ACK", "page", dict(statuses=[(IDLE, 0)], ack_delay={2: 1.5}), 0, ["STATUS"] + PAGE),
    ("NAK on a band: job fails", "page", dict(statuses=[(IDLE, 0)], nak_band=1), 4, ["STATUS", "BAND"]),
    ("noise and bad CRC before replies", "page", dict(statuses=[(IDLE, 0)], noise=True), 0, ["STATUS"] + PAGE),
    ("replies split byte by byte", "page", dict(statuses=[(IDLE, 0)], split=True), 0, ["STATUS"] + PAGE),
    ("21 rows: three full bands", "tall", dict(statuses=[(IDLE, 0)]), 0, ["STATUS", "BAND", "BAND", "BAND", "FEED"]),
]


def main():
    app = sys.argv[1] if len(sys.argv) > 1 else "./test_app"
    failures = 0

    r = subprocess.run([app, "darkness"], capture_output=True, text=True)
    print(("ok  " if r.returncode == 0 else "FAIL") + "  darkness map, centring, driver data")
    if r.returncode:
        print(r.stderr); failures += 1

    for name, arg, kw, want_rc, want_cmds in SCENARIOS:
        pico = FakePico(**kw)
        r = subprocess.run([app, arg, pico.path], capture_output=True, text=True, timeout=60)
        time.sleep(0.05)
        cmds = [NAMES.get(c, c) for c, _ in pico.frames]
        problems = []
        if r.returncode != want_rc:
            problems.append(f"rc {r.returncode}, want {want_rc}")
        if cmds != want_cmds:
            problems.append(f"commands {cmds}, want {want_cmds}")
        bands = [p for c, p in pico.frames if c == BAND]
        try:
            if want_rc == 0:
                rows = [7] * len(bands) if arg == "tall" else [7, 3]
                for p, n in zip(bands, rows):
                    check_band(p, n)
                feeds = [p for c, p in pico.frames if c == FEED]
                assert feeds == [bytes([40, 0])], f"feed {feeds}"
        except AssertionError as e:
            problems.append(str(e))
        print(("ok  " if not problems else "FAIL") + "  " + name)
        for p in problems:
            print("      " + p)
        if problems:
            print(r.stderr); failures += 1
        pico.stop = True
        os.close(pico.master); os.close(pico.slave)

    # Printer status callback: paper-out error -> media-empty reason.
    pico = FakePico(statuses=[(ERROR, 4)])
    r = subprocess.run([app, "status", pico.path], capture_output=True, text=True, timeout=10)
    ok = r.returncode == 0 and "reasons=0x80" in r.stdout
    print(("ok  " if ok else "FAIL") + "  status: paper-out -> media-empty")
    if not ok:
        print(r.stdout, r.stderr); failures += 1
    os.close(pico.master); os.close(pico.slave)

    print(f"{failures} failure(s)" if failures else "all passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
