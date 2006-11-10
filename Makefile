CC = gcc
OPTS = -O2 -std=c99
LIBS = -lm

build:
	$(CC) $(OPTS) calibrator.c -o calibrator $(LIBS)

clean:
	rm calibrator	
