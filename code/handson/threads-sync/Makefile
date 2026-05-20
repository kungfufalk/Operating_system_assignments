CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=gnu11

# Programs that use pthreads
PTHREAD_PROGS = thread-shared-mem thread-deadlock thread-futex

ALL = $(PTHREAD_PROGS)

all: $(ALL)

$(PROGS): %: %.c
	$(CC) $(CFLAGS) -o $@ $<

$(PTHREAD_PROGS): %: %.c
	$(CC) $(CFLAGS) -pthread -o $@ $<

clean:
	rm -f $(ALL)

.PHONY: all clean
