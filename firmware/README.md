# Firmware

Pico SDK project (Pico 2 / RP2350). Build:

    export PICO_SDK_PATH=~/pico-sdk
    cmake -B build && cmake --build build
    # hold BOOTSEL, copy build/silentype.uf2

Layout:
- `src/config.h` — pin map + ALL timing constants. `grep TBD_FROM_ROM` must return
  nothing before the head rail is connected. `HEAD_DOTS` is unverified (7).
- `src/print_engine.c` — core1 engine. Two-layer head safety (U5 hardware one-shot
  re-armed by GP9 strobe; software timer guard). Latched error state; RESET recovers.
- `src/main.c` — core0 CDC frame parser with resync, CRC, ACK/NAK, back-pressure.
- `src/usb_descriptors.c`, `src/tusb_config.h` — CDC-ACM device.

Syntax-checked against stub headers; **not yet compiled against the real SDK**
(no SDK in the authoring environment). Expect minor API nits on first real build.
UART debug on GP0/GP1 @115200.
