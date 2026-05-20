/*
 * proc-identify.c
 *
 * Demonstrates how a Linux process identifies itself, and how it can
 * modify its own identity as seen by the kernel and by tools like
 * ps(1), top(1), and /proc.
 *
 * The program covers:
 *   1. argv[0]           -- the name passed by the parent (shell) at exec
 *   2. /proc/self/exe    -- symlink to the actual executable on disk
 *   3. /proc/self/comm   -- the kernel "command name" (max 15 chars + NUL)
 *   4. /proc/self/cmdline-- the full command line (NUL-separated args)
 *   5. prctl(PR_SET_NAME)-- change the kernel comm / thread name
 *   6. Overwriting argv  -- change what /proc/self/cmdline reports
 *
 * Build:
 *   gcc -Wall -o proc-identify proc-identify.c
 *
 * Run:
 *   ./proc-identify foo bar baz
 *   (pass a few arguments so cmdline is interesting)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/prctl.h>

/* Maximum buffer size for reading small /proc files */
#define BUF_SIZE 4096

/* ------------------------------------------------------------------ */
/* Helper: read and print /proc/self/comm                             */
/*                                                                    */
/* /proc/self/comm contains the "command name" of the calling thread. */
/* The kernel truncates it to 15 characters (TASK_COMM_LEN - 1).     */
/* This is the name shown in the "COMMAND" column of top(1) and in   */
/* the "comm" field of ps -o comm.                                   */
/* ------------------------------------------------------------------ */
static void show_comm(void)
{
	char buf[BUF_SIZE];
	FILE *fp;

	fp = fopen("/proc/self/comm", "r");
	if (!fp) {
		perror("fopen(/proc/self/comm)");
		return;
	}

	/* comm is a single line, newline-terminated */
	if (fgets(buf, sizeof(buf), fp))
		printf("  /proc/self/comm : %s", buf);   /* already has \n */

	fclose(fp);
}

/* ------------------------------------------------------------------ */
/* Helper: read and print /proc/self/cmdline                          */
/*                                                                    */
/* /proc/self/cmdline contains the process's command-line arguments   */
/* separated by NUL ('\0') bytes, not spaces. We replace the NULs    */
/* with spaces for display.                                           */
/*                                                                    */
/* The kernel reads this data directly from the process's user-space  */
/* memory -- the same memory that holds the argv[] strings passed to  */
/* main(). This is why overwriting that memory changes the output.    */
/* ------------------------------------------------------------------ */
static void show_cmdline(void)
{
	char buf[BUF_SIZE];
	ssize_t n;
	int fd;

	/*
	 * We use read(2) instead of fgets() because the content contains
	 * embedded NUL bytes that would confuse C string functions.
	 */
	fd = open("/proc/self/cmdline", 0 /* O_RDONLY */);
	if (fd < 0) {
		perror("open(/proc/self/cmdline)");
		return;
	}

	n = read(fd, buf, sizeof(buf) - 1);
	close(fd);

	if (n <= 0)
		return;

	/*
	 * Replace every NUL separator with a space so we can print the
	 * whole command line as a single readable string.
	 */
	for (ssize_t i = 0; i < n - 1; i++) {
		if (buf[i] == '\0')
			buf[i] = ' ';
	}
	buf[n] = '\0';

	printf("  /proc/self/cmdline : %s\n", buf);
}

/* ------------------------------------------------------------------ */
/* Helper: read and print /proc/self/exe                              */
/*                                                                    */
/* /proc/self/exe is a symbolic link that points to the actual binary */
/* file on disk that was exec'd. readlink(2) resolves it.             */
/* Unlike comm and cmdline, this cannot be changed at runtime -- it   */
/* always reflects the original executable.                           */
/* ------------------------------------------------------------------ */
static void show_exe(void)
{
	char buf[BUF_SIZE];
	ssize_t n;

	/*
	 * readlink(2) reads the target of a symbolic link.
	 * It does NOT append a NUL terminator, so we must add one.
	 */
	n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
	if (n < 0) {
		perror("readlink(/proc/self/exe)");
		return;
	}
	buf[n] = '\0';

	printf("  /proc/self/exe  : %s\n", buf);
}

/* ------------------------------------------------------------------ */
/* Helper: print all identity information at once                     */
/* ------------------------------------------------------------------ */
static void show_all(const char *argv0)
{
	printf("  argv[0]         : %s\n", argv0);
	show_exe();
	show_comm();
	show_cmdline();
}

/* ------------------------------------------------------------------ */
/* Helper: pause and wait for the user to press Enter                 */
/*                                                                    */
/* This gives the student time to inspect the process from another    */
/* terminal using commands like:                                      */
/*   ps -p <PID> -o pid,comm,args                                    */
/*   cat /proc/<PID>/comm                                             */
/*   cat /proc/<PID>/cmdline | tr '\0' ' '                           */
/*   ls -l /proc/<PID>/exe                                           */
/* ------------------------------------------------------------------ */
static void pause_for_inspection(void)
{
	printf("\n  PID = %d -- inspect with:\n", getpid());
	printf("    ps -p %d -o pid,comm,args,cmd\n", getpid());
	printf("    cat /proc/%d/comm\n", getpid());
	printf("    cat /proc/%d/cmdline | tr '\\0' ' '; echo\n", getpid());
	printf("    ls -l /proc/%d/exe\n", getpid());
	printf("\n  Press Enter to continue...");
	fflush(stdout);
	getchar();
}

/* ================================================================== */
/* main                                                               */
/* ================================================================== */
int main(int argc, char *argv[])
{
	char new_name[] = "my-new-name";

	printf("=== Process Identity Demo ===\n");
	printf("  PID: %d\n\n", getpid());

	/* -------------------------------------------------------------- */
	/* STEP 1: Show the initial identity of the process               */
	/*                                                                */
	/* At this point everything reflects the original exec:           */
	/*  - argv[0] is whatever the shell (or parent) passed            */
	/*  - comm is derived from the executable filename (max 15 chars) */
	/*  - cmdline is the full argument vector                         */
	/*  - exe points to the binary on disk                            */
	/* -------------------------------------------------------------- */
	printf("[Step 1] Initial process identity:\n");
	show_all(argv[0]);
	pause_for_inspection();

	/* -------------------------------------------------------------- */
	/* STEP 2: Change the "comm" (process/thread name) via prctl()    */
	/*                                                                */
	/* prctl(PR_SET_NAME, name) sets the calling thread's name.       */
	/* This is what appears in:                                       */
	/*   - /proc/self/comm                                            */
	/*   - the "Name:" field in /proc/self/status                     */
	/*   - the output of `ps -o comm` and `top`                       */
	/*                                                                */
	/* The name is truncated to 15 characters (TASK_COMM_LEN - 1).   */
	/*                                                                */
	/* Note: this does NOT change argv[0], cmdline, or exe.           */
	/* -------------------------------------------------------------- */
	printf("\n[Step 2] Changing comm via prctl(PR_SET_NAME, \"%s\"):\n",
	       new_name);

	if (prctl(PR_SET_NAME, new_name) < 0) {
		perror("prctl(PR_SET_NAME)");
		return 1;
	}

	show_all(argv[0]);
	pause_for_inspection();

	/* -------------------------------------------------------------- */
	/* STEP 3: Verify with prctl(PR_GET_NAME)                        */
	/*                                                                */
	/* We can also read back the thread name programmatically.        */
	/* The buffer must be at least 16 bytes (TASK_COMM_LEN).          */
	/* -------------------------------------------------------------- */
	{
		char current_name[16];
		if (prctl(PR_GET_NAME, current_name) == 0)
			printf("\n[Step 3] prctl(PR_GET_NAME) returns: \"%s\"\n",
			       current_name);
	}

	/* -------------------------------------------------------------- */
	/* STEP 4: Modify argv[0] to change /proc/self/cmdline            */
	/*                                                                */
	/* The kernel's /proc/<pid>/cmdline reads directly from the       */
	/* process's user-space memory where the argument strings live.   */
	/* These strings are placed on the stack by the kernel at exec    */
	/* time, just above the environment variables.                    */
	/*                                                                */
	/* Because the kernel reads from that memory at access time (not  */
	/* a snapshot), overwriting argv[0] in place changes what         */
	/* /proc/self/cmdline reports.                                    */
	/*                                                                */
	/* IMPORTANT CAVEATS:                                             */
	/*  - We can only write up to the original length of argv[0].    */
	/*    Writing beyond that would overwrite argv[1] or the          */
	/*    environment, which is dangerous.                            */
	/*  - Some programs (e.g., PostgreSQL, nginx) use a more          */
	/*    elaborate technique: they relocate environ[] to the heap    */
	/*    and then treat the entire original argv+environ area as     */
	/*    a buffer for the new command line.                          */
	/* -------------------------------------------------------------- */
	printf("\n[Step 4] Modifying argv[0] to change /proc/self/cmdline.\n");
	printf("  Original argv[0] = \"%s\" (length %zu)\n",
	       argv[0], strlen(argv[0]));

	{
		char *fake_name = "REPLACED";
		size_t orig_len = strlen(argv[0]);
		size_t fake_len = strlen(fake_name);

		/*
		 * Zero out the old argv[0] content first, then copy in
		 * the new name. We must stay within the original bounds.
		 */
		if (fake_len > orig_len) {
			printf("  WARNING: new name is longer than argv[0],"
			       " truncating.\n");
			fake_len = orig_len;
		}

		/* Clear old content */
		memset(argv[0], 0, orig_len);

		/* Write new content (without the NUL -- memset already placed it) */
		memcpy(argv[0], fake_name, fake_len);
	}

	printf("\n  After overwriting argv[0]:\n");
	show_all(argv[0]);
	pause_for_inspection();

	/* -------------------------------------------------------------- */
	/* STEP 5: Overwrite the ENTIRE argument vector                   */
	/*                                                                */
	/* The argv strings are laid out contiguously in memory:          */
	/*   argv[0]\0argv[1]\0argv[2]\0...                              */
	/*                                                                */
	/* By computing the total span from argv[0] to the end of the    */
	/* last argument, we can overwrite the whole region with a single */
	/* fake command line.                                             */
	/* -------------------------------------------------------------- */
	if (argc > 1) {
		printf("\n[Step 5] Overwriting the entire argv region.\n");

		/*
		 * Find the total contiguous size of all argv strings.
		 * argv[last] starts at some address; its content ends at
		 * argv[last] + strlen(argv[last]).
		 */
		char *argv_start = argv[0];
		char *argv_end   = argv[argc - 1] + strlen(argv[argc - 1]);
		size_t total_len = (size_t)(argv_end - argv_start);

		printf("  argv region: %zu bytes (from argv[0] to end of argv[%d])\n",
		       total_len, argc - 1);

		/* Clear the entire region */
		memset(argv_start, 0, total_len);

		/* Write a completely new "command line" into the region */
		char *fake_cmdline = "I-am-a-teapot --short --stout";
		size_t fake_len = strlen(fake_cmdline);
		if (fake_len > total_len - 1)
			fake_len = total_len - 1;

		memcpy(argv_start, fake_cmdline, fake_len);

		printf("\n  After overwriting entire argv region:\n");
		show_all(argv[0]);
		pause_for_inspection();
	}

	/* -------------------------------------------------------------- */
	/* Summary                                                        */
	/* -------------------------------------------------------------- */
	printf("\n=== Summary ===\n");
	printf("  /proc/self/exe     -- always points to the real binary (immutable)\n");
	printf("  /proc/self/comm    -- changeable via prctl(PR_SET_NAME)\n");
	printf("  /proc/self/cmdline -- changeable by overwriting argv[] in memory\n");
	printf("  argv[0]            -- just a C pointer; the kernel does not track it\n");
	printf("\nDone.\n");

	return 0;
}
