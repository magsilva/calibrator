CC = gcc
CLANG++ = clang
OPTS = -O2 -std=c99 -D_DEFAULT_SOURCE
FUZZ_OPTIONS = -O2 -std=c99 -D_DEFAULT_SOURCE -fsanitize=fuzzer-no-link,address,signed-integer-overflow -fsanitize-coverage=trace-cmp
LIBS = -lm

build:
	$(CC) $(OPTS) calibrator.c -o calibrator $(LIBS)

build-fuzzer:
	$(CLANG++) $(FUZZ_OPTIONS) calibrator.c -o calibrator $(LIBS)

clean:
	rm calibrator	
