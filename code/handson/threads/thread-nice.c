/*
 * thread-nice.c
 *
 * Demonstrates how the nice value affects CPU time distribution
 * between threads competing for the same CPU core.
 *
 * The program:
 *   1. Pins itself to a single CPU core (CPU 0) using
 *      sched_setaffinity(), so both threads MUST share one core.
 *   2. Creates two threads with different nice values.
 *   3. Both threads busy-loop for a fixed duration, counting
 *      iterations.
 *   4. Compares how many iterations each thread completed and how
 *      much CPU time each received.
 *
 * The thread with the LOWER nice value (= higher priority) will
 * get more CPU time and complete more iterations.
 *
 * Background -- nice values and the CFS scheduler:
 *
 *   Linux's Completely Fair Scheduler (CFS) assigns each task a
 *   "weight" derived from its nice value.  The CPU time a task
 *   receives is proportional to its weight relative to other
 *   runnable tasks.
 *
 *   Nice ranges from -20 (highest priority) to +19 (lowest).
 *   The default is 0.  Each nice level roughly corresponds to a
 *   ~10% change in CPU share vs. the adjacent level.
 *
 *   Examples (approximate ratios when two tasks compete):
 *     nice 0 vs nice  5  →  ~3:1  CPU time ratio
 *     nice 0 vs nice 10  →  ~9:1
 *     nice 0 vs nice 19  → ~68:1
 *
 *   Lowering nice (raising priority) below 0 requires root or
 *   CAP_SYS_NICE.  Raising nice (lowering priority) is unprivileged.
 *
 * Build:
 *   gcc -Wall -pthread -o thread-nice thread-nice.c
 *
 * Usage:
 *   ./thread-nice [NICE_A] [NICE_B] [SECONDS]
 *
 *   NICE_A   - nice value for thread A (default: 0)
 *   NICE_B   - nice value for thread B (default: 10)
 *   SECONDS  - how long each thread runs (default: 5)
 *
 * Examples:
 *   ./thread-nice              # nice 0 vs 10, 5 seconds
 *   ./thread-nice 0 19 3      # nice 0 vs 19, 3 seconds
 *   ./thread-nice 0 0 5       # same nice, expect ~1:1 ratio
 *
 * NOTE: Setting a negative nice value requires root (or CAP_SYS_NICE).
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sched.h>
#include <pthread.h>
#include <sys/resource.h>
#include <sys/syscall.h>

/* ------------------------------------------------------------------ */
/* Per-thread arguments and results                                   */
/* ------------------------------------------------------------------ */
struct thread_info {
	int    id;          /* thread label (A=0, B=1) */
	int    nice_val;    /* nice value to set       */
	int    run_secs;    /* how long to busy-loop   */

	/* Filled in by the thread before it exits: */
	long   iterations;  /* number of loop iterations completed */
	double cpu_time;    /* CPU time consumed (seconds)         */
};

/* ------------------------------------------------------------------ */
/* Get wall-clock time in seconds (monotonic, not affected by NTP)    */
/* ------------------------------------------------------------------ */
static double now_monotonic(void)
{
	struct timespec ts;

	/*
	 * CLOCK_MONOTONIC is a clock that advances uniformly and is not
	 * affected by system time adjustments (e.g., NTP, settimeofday).
	 * It is the right clock for measuring elapsed wall-clock time.
	 */
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* ------------------------------------------------------------------ */
/* Get per-thread CPU time in seconds                                 */
/* ------------------------------------------------------------------ */
static double thread_cpu_time(void)
{
	struct timespec ts;

	/*
	 * CLOCK_THREAD_CPUTIME_ID measures the CPU time consumed by
	 * the CALLING THREAD only -- time spent sleeping or waiting
	 * does not count.  This is exactly what we want: how much of
	 * the CPU's execution time went to this thread.
	 */
	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* ------------------------------------------------------------------ */
/* Thread function: set nice value, then busy-loop                    */
/* ------------------------------------------------------------------ */
static void *worker_fn(void *arg)
{
	struct thread_info *ti = (struct thread_info *)arg;
	char label = 'A' + ti->id;
	double deadline;

	/*
	 * Set this thread's nice value.
	 *
	 * setpriority(PRIO_PROCESS, 0, nice) sets the scheduling
	 * priority of the calling thread.  Despite the name
	 * "PRIO_PROCESS", on Linux (with NPTL threads) passing
	 * tid=0 means "the calling thread".
	 *
	 * Raising nice (lowering priority, e.g., 0 → 10) is always
	 * allowed.  Lowering nice (raising priority, e.g., 0 → -5)
	 * requires root or CAP_SYS_NICE.
	 *
	 * We use setpriority() instead of nice() because nice()
	 * returns -1 on error AND as a legitimate value (nice -1),
	 * making error detection awkward.
	 */
	errno = 0;
	if (setpriority(PRIO_PROCESS, 0, ti->nice_val) == -1 && errno != 0) {
		fprintf(stderr, "[Thread %c] setpriority(nice=%d): %s\n",
			label, ti->nice_val, strerror(errno));
		if (errno == EACCES)
			fprintf(stderr, "  (try running as root for "
				"negative nice values)\n");
		return NULL;
	}

	printf("[Thread %c] nice=%d, starting %d-second busy loop...\n",
	       label, ti->nice_val, ti->run_secs);

	/*
	 * Busy-loop until the wall-clock deadline.
	 *
	 * Both threads start at roughly the same time and run for the
	 * same wall-clock duration.  Since they are pinned to ONE core,
	 * the scheduler must interleave them.  The thread with the
	 * lower nice value will be scheduled more often and complete
	 * more iterations.
	 */
	ti->iterations = 0;
	deadline = now_monotonic() + ti->run_secs;

	while (now_monotonic() < deadline)
		ti->iterations++;

	/* Record how much CPU time THIS thread consumed */
	ti->cpu_time = thread_cpu_time();

	printf("[Thread %c] Finished: %ld iterations, %.3f s CPU time.\n",
	       label, ti->iterations, ti->cpu_time);

	return NULL;
}

/* ================================================================== */
/* main                                                               */
/* ================================================================== */
int main(int argc, char *argv[])
{
	struct thread_info info[2];
	pthread_t threads[2];
	int nice_a    = 0;
	int nice_b    = 10;
	int run_secs  = 5;
	int ret;

	setbuf(stdout, NULL);

	/* Parse optional command-line arguments */
	if (argc >= 2) nice_a   = atoi(argv[1]);
	if (argc >= 3) nice_b   = atoi(argv[2]);
	if (argc >= 4) run_secs = atoi(argv[3]);

	if (run_secs < 1) {
		fprintf(stderr, "SECONDS must be >= 1\n");
		return 1;
	}

	printf("=== Nice Value vs. CPU Time Demo ===\n\n");

	/* -------------------------------------------------------------- */
	/* Pin the entire process to CPU 0                                */
	/*                                                                */
	/* sched_setaffinity() restricts which CPUs a task may run on.    */
	/* By confining both threads to a single core, they MUST compete  */
	/* for the same CPU -- the scheduler cannot just put them on      */
	/* different cores, which would hide the priority difference.     */
	/*                                                                */
	/* cpu_set_t is a bitmask of CPUs.  CPU_ZERO clears it,           */
	/* CPU_SET adds a specific CPU.  Passing pid=0 means "this        */
	/* process" (all its threads inherit the affinity).               */
	/* -------------------------------------------------------------- */
	{
		cpu_set_t cpuset;
		CPU_ZERO(&cpuset);
		CPU_SET(0, &cpuset);

		if (sched_setaffinity(0, sizeof(cpuset), &cpuset) == -1) {
			perror("sched_setaffinity");
			fprintf(stderr, "(could not pin to CPU 0)\n");
			return 1;
		}
		printf("  Pinned to CPU 0 (sched_setaffinity).\n");
	}

	printf("  Thread A nice : %d\n", nice_a);
	printf("  Thread B nice : %d\n", nice_b);
	printf("  Duration      : %d seconds\n\n", run_secs);

	printf("──────────────────────────────────────────\n\n");

	/* Set up thread arguments */
	info[0] = (struct thread_info){ .id = 0, .nice_val = nice_a,
					.run_secs = run_secs };
	info[1] = (struct thread_info){ .id = 1, .nice_val = nice_b,
					.run_secs = run_secs };

	/* Create both threads */
	for (int i = 0; i < 2; i++) {
		ret = pthread_create(&threads[i], NULL, worker_fn, &info[i]);
		if (ret != 0) {
			fprintf(stderr, "pthread_create: %s\n", strerror(ret));
			return 1;
		}
	}

	/* Wait for both to finish */
	for (int i = 0; i < 2; i++)
		pthread_join(threads[i], NULL);

	/* -------------------------------------------------------------- */
	/* Print comparison                                               */
	/* -------------------------------------------------------------- */
	printf("\n──────────────────────────────────────────\n");
	printf("\n  Results (both threads ran for %d wall-clock seconds):\n\n",
	       run_secs);

	printf("  %-12s %8s %14s %12s\n",
	       "Thread", "Nice", "Iterations", "CPU time");
	printf("  %-12s %8s %14s %12s\n",
	       "──────", "────", "──────────", "────────");
	printf("  %-12s %8d %14ld %11.3f s\n",
	       "Thread A", nice_a, info[0].iterations, info[0].cpu_time);
	printf("  %-12s %8d %14ld %11.3f s\n",
	       "Thread B", nice_b, info[1].iterations, info[1].cpu_time);

	/* Show the ratio, avoiding division by zero */
	if (info[1].iterations > 0 && info[1].cpu_time > 0.0) {
		printf("\n  Iteration ratio (A / B): %.2f : 1\n",
		       (double)info[0].iterations / info[1].iterations);
		printf("  CPU time  ratio (A / B): %.2f : 1\n",
		       info[0].cpu_time / info[1].cpu_time);
	}

	printf("\n  The thread with the lower nice value (= higher priority)\n"
	       "  received more CPU time on the shared core.\n");

	printf("\n  Try again with equal nice values to see a ~1:1 ratio:\n");
	printf("    ./thread-nice 0 0\n");

	printf("\nDone.\n");
	return 0;
}
