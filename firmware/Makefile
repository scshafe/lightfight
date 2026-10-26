# Host tests of the portable C library (lib/). No ESP-IDF here: the firmware
# build is milestone M5. `make test` is run by scripts/verify.
CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -O1 -g
BUILD := build
TESTS := $(patsubst tests/%.c,$(BUILD)/%,$(wildcard tests/test_*.c))
LIB := $(wildcard lib/*.c)

.PHONY: test clean
test: $(TESTS)
	@for t in $(TESTS); do ./$$t || exit 1; done

$(BUILD)/%: tests/%.c $(LIB) $(wildcard lib/*.h) tests/lf_test.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -DVECTORS='"$(CURDIR)/../protocol/ir-vectors.csv"' -DBLE_VECTORS='"$(CURDIR)/../protocol/ble-vectors.csv"' -o $@ $< $(LIB)

clean:
	rm -rf $(BUILD)
