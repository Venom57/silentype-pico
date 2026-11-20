# Host-side tests for the core1 print engine and core0 frame parser.
# Builds the REAL firmware sources against stubs/ (minimal Pico SDK + TinyUSB
# surface) and sim.c (virtual clock; sr_write() records every latched word).
#   make        build + run all tests
#   make clean
CC      ?= gcc
SRC     := ../src
B       := build
CFLAGS  := -std=c11 -Wall -Wextra -O1 -g -Istubs -I$(SRC)

TESTS := $(B)/test_timing $(B)/test_queue

all: $(TESTS)
	@for t in $(TESTS); do ./$$t || exit 1; done

$(B):
	mkdir -p $@

$(B)/print_engine.o: $(SRC)/print_engine.c $(wildcard $(SRC)/*.h) | $(B)
	$(CC) $(CFLAGS) -c $< -o $@
# main.c is compiled standalone too, only to see its warnings (test_queue includes it).
$(B)/main.o: $(SRC)/main.c $(wildcard $(SRC)/*.h) | $(B)
	$(CC) $(CFLAGS) -Dmain=fw_main -c $< -o $@
$(B)/crc8.o: $(SRC)/crc8.c | $(B)
	$(CC) $(CFLAGS) -c $< -o $@
$(B)/sim.o: sim.c stubs/sim.h stubs/tusb.h | $(B)
	$(CC) $(CFLAGS) -c $< -o $@

$(B)/test_timing: test_timing.c $(B)/print_engine.o $(B)/sim.o
	$(CC) $(CFLAGS) $^ -o $@
$(B)/test_queue: test_queue.c $(SRC)/main.c $(B)/main.o $(B)/print_engine.o $(B)/sim.o $(B)/crc8.o
	$(CC) $(CFLAGS) test_queue.c $(B)/print_engine.o $(B)/sim.o $(B)/crc8.o -o $@

clean:
	rm -rf $(B)

.PHONY: all clean
