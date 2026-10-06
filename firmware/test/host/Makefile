# Host tests of the signal chain: make (checks), make wav (plus WAV files in build/wav)
SRC := ../../main
# CFLAGS from the environment replace -O2 -g only, the checks stay
CFLAGS ?= -O2 -g
override CFLAGS += -Wall -Wextra -Werror -std=c11 -D_DEFAULT_SOURCE
OBJS := build/dsp_test.o build/dsp.o build/pitch.o

.PHONY: test wav clean
test: build/dsp_test
	./build/dsp_test

wav: build/dsp_test
	rm -rf build/wav
	./build/dsp_test --wav build/wav

build/dsp_test: $(OBJS)
	$(CC) -o $@ $^ -lm

build/%.o: %.c $(SRC)/dsp.h $(SRC)/pitch.h | build
	$(CC) $(CFLAGS) -I$(SRC) -c -o $@ $<

build/%.o: $(SRC)/%.c $(SRC)/dsp.h $(SRC)/pitch.h | build
	$(CC) $(CFLAGS) -I$(SRC) -c -o $@ $<

build:
	mkdir -p build

clean:
	rm -rf build
