CC = gcc
CFLAGS = -Wall -Wextra -O2

TARGETS = files fork fork-execvp fork-files fork-pread fork-pipes fork-waitpid fork-memory

all: $(TARGETS)

files: files.c
	$(CC) $(CFLAGS) -o files files.c

fork: fork.c
	$(CC) $(CFLAGS) -o fork fork.c

fork-execvp: fork-execvp.c
	$(CC) $(CFLAGS) -o fork-execvp fork-execvp.c

fork-files: fork-files.c
	$(CC) $(CFLAGS) -o fork-files fork-files.c

fork-pread: fork-pread.c
	$(CC) $(CFLAGS) -o fork-pread fork-pread.c

fork-pipes: fork-pipes.c
	$(CC) $(CFLAGS) -o fork-pipes fork-pipes.c

fork-waitpid: fork-waitpid.c
	$(CC) $(CFLAGS) -o fork-waitpid fork-waitpid.c

fork-memory: fork-memory.c
	$(CC) $(CFLAGS) -o fork-memory fork-memory.c

clean:
	rm -f $(TARGETS)

.PHONY: all clean
