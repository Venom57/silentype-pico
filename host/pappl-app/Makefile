# PAPPL >= 1.4 via pkg-config. PAPPL_CFLAGS can point at a source tree instead:
#   make test PAPPL_CFLAGS="-I/path/to/pappl-1.4.12 $(pkg-config --cflags cups)"
PAPPL_CFLAGS ?= $(shell pkg-config --cflags pappl)
PAPPL_LIBS   ?= $(shell pkg-config --libs pappl)
CFLAGS       += -std=gnu11 -Wall -Wextra -O2

silentype-app: silentype-app.c
	$(CC) $(CFLAGS) $(PAPPL_CFLAGS) -o $@ $< $(PAPPL_LIBS)

# Runs the app's device and raster code against a fake Pico on a pty (no libpappl needed).
test: test/test_app
	python3 test/fake_pico.py test/test_app

test/test_app: test/test_app.c test/stubs.c silentype-app.c
	$(CC) $(CFLAGS) -Werror $(PAPPL_CFLAGS) -o $@ test/test_app.c test/stubs.c

clean:
	rm -f silentype-app test/test_app

.PHONY: test clean
