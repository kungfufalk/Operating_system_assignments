# Makefile (build only)

CC      := gcc
CFLAGS  := -O2 -Wall -Wextra -g
LDFLAGS :=

BINARIES := print alloc fork sleep

.PHONY: all clean

all: $(BINARIES)

print: print.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

alloc: alloc.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

fork: fork.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

fork: sleep.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)
clean:
	rm -f $(BINARIES) *.o
