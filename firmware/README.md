# Firmware

Pico SDK project for the Raspberry Pi Pico 2 (RP2350, `PICO_BOARD=pico2`). Turns queued
7-dot bands into exactly timed 16-bit words for the Silentype's 74LS673 latch.

    export PICO_SDK_PATH=~/pico-sdk
    cmake -B build && cmake --build build
    # hold BOOTSEL, plug in USB, copy build/silentype.uf2

**Not yet built against the real SDK** (no SDK in the authoring environment); it compiles
cleanly in the host-side test harness. Expect minor API nits on the first real build.
Debug UART: GP0/GP1 at 115200 (carrier header J3). USB is the band-protocol link.

## Layout

| File | Role |
|---|---|
| `src/main.c` | Core 0: TinyUSB CDC, frame parser (resync, CRC, ACK/NAK, back-pressure) |
| `src/print_engine.c` | Core 1: band/feed queue, fixed-period column schedule, motion, homing, error state, SET_PARAM, STATUS; plus the core-0 guard alarm |
| `src/shiftreg.c`, `src/shiftreg.h` | The only path to the printer: `sr_init`, `sr_write`, `sr_make`, `sr_bus_enable` |
| `src/silentype_sr.pio` | One PIO state machine: 16 bits MSB first on SD/SC, then the store (XC) with SC low |
| `src/config.h` | Pin map, word layout, stepper tables, 8-level burn tables, safety limits |
| `src/protocol.h` | Wire format shared with the host tools |
| `src/usb_descriptors.c`, `src/tusb_config.h` | One CDC-ACM interface (VID 0x1209 / PID 0x0001 — a pid.codes test ID; get a real PID before sharing binaries) |
| `test/` | Host-side harness: SDK stubs, virtual-clock simulator, `test_timing`, `test_queue` |

## Pins (carrier v1.0)

| GPIO | Use |
|---|---|
| GP2 | SD — serial data (PIO OUT) |
| GP3 | SC — shift clock (side-set bit 0) |
| GP4 | XC — store clock (side-set bit 1; must be GP3 + 1) |
| GP5 | MS — margin switch, active low |
| GP6 | Bus enable: high = 74AHCT541 drives the bus; low/reset = bus released, printer cleared |
| GP22 / GP26 | Feed button / error LED (defined; not yet used) |

## How it works

- **Transport.** One `sr_write()` = one atomic PIO transaction (~48 µs): SD set while SC is
  high, SC low pulse, rising edge shifts; then SC low → XC pulse (latch) → SC high. SC and XC
  are never high together during a write (that combination clears the printer). `sr_init()`
  keeps the bus released until the PIO is running, clears the printer, then enables the bus.
- **Column timing.** Every column has the same period at a given intensity, whatever it prints:
  pre-heat slot (dots cold for two columns), burn slot, off slot, scheduled against the column's
  start time. P = PRE + BURN + max(OFF, 100 µs): 2489 µs at level 1, 3244 µs at level 7.
- **Motion.** Apple's one-hot wave drive {1, 8, 4, 2}; head off on every step word;
  bidirectional passes; homing against the margin switch (3 s time-out); paper feed 4032 µs/step.
- **Queue.** 8 entries of bands *and* feeds, in order. When full, the parser stops reading USB
  and withholds the ACK. An ACK means queued, not printed.
- **Safety.** Per-pulse cap 3000 µs; a core-0 alarm drops GP6 if a burn overruns (no PIO access
  from the IRQ); errors latch until RESET.

`grep TBD_FROM_ROM src/config.h` lists the values still waiting for bench measurements.

## Tests

    make -C test

Runs the real `print_engine.c` and `main.c` against stub SDK headers with a virtual clock and a
recorder of every latched word: `test_timing` (identical column periods at several levels, both
directions; head off on step words; no pulse over the cap) and `test_queue` (FEED ordering,
back-pressure, invalid/error replies, RESET).
