CC = gcc
CLANG++ = clang++
OPTS = -O2 -std=c99
FUZZ_OPTIONS = -fsanitize=fuzzer,address
LIBS = -lm

build:
	$(CC) $(OPTS) calibrator.c -o calibrator $(LIBS)

build-fuzzer:
	$(CLANG++) $(OPTS) $(FUZZ_OPTIONS) calibrator.c -o calibrator $(LIBS)

clean:
	rm calibrator	
