CC ?= gcc
CFLAGS ?= -O2
HOSTCC ?= cc
HOST_CFLAGS ?=

.PHONY: all clean test FORCE

all: fh8626-ptz

# The package source directory may contain developer-built artifacts. Always
# relink with TARGET_CC when Buildroot consumes this local package.
FORCE:

fh8626-ptz: fh8626_ptz.c FORCE
	$(CC) $(CFLAGS) -Wall -Wextra -Werror -o $@ $<

test: fh8626-ptz-tests
	./fh8626-ptz-tests

fh8626-ptz-tests: fh8626_ptz_tests.c fh8626_ptz.c
	$(HOSTCC) $(HOST_CFLAGS) -O2 -Wall -Wextra -Werror -Wno-unused-function -o $@ $<

clean:
	rm -f fh8626-ptz fh8626-ptz-tests
