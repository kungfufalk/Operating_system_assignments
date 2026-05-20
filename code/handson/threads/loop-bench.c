/*
 * loop-bench.c
 *
 * A simple busy-loop benchmark that runs for a configurable duration
 * on a single CPU core, counting iterations and reporting CPU time.
 *
 * This program is designed to be combined with CLI tools like
 * taskset(1) and chrt(1) to demonstrate how Linux scheduler classes
 * and priorities affect CPU time distribution between processes.
 *
 * The program pins itself to a single CPU (default: CPU 0) and then
 * busy-loops, printing periodic progress updates so you can see the
 * effect of scheduling in real time.
 *
 * Build:
 *   gcc -Wall -o loop-bench loop-bench.c
 *
 * Usage:
 *   ./loop-bench [SECONDS] [CPU] [LABEL]
 *
 *   SECONDS - duration of the busy loop (default: 5)
 *   CPU     - which CPU core to pin to (default: 0)
 *   LABEL   - a short name for this instance (default: "bench")
 *
 * Examples:
 *
 *   # --- Nice values (same scheduler class: SCHED_OTHER) ---
 *   # Terminal 1:
 *   nice -n 0  ./loop-bench 5 0 normal
 *   # Terminal 2:
 *   nice -n 19 ./loop-bench 5 0 nice19
 *
 *   # --- Scheduler classes: SCHED_FIFO vs SCHED_OTHER ---
 *   # Terminal 1 (SCHED_FIFO, priority 1 -- needs root):
 *   sudo chrt -f 1 ./loop-bench 5 0 FIFO
 *   # Terminal 2 (SCHED_OTHER, default):
 *   ./loop-bench 5 0 OTHER
 *
 *   The FIFO process will consume 100% of the CPU for its entire
 *   duration.  The OTHER process will get almost ZERO CPU time
 *   while FIFO is running, because SCHED_FIFO always preempts
 *   SCHED_OTHER and never voluntarily yields.
 *
 *   # --- Scheduler classes: SCHED_RR vs SCHED_OTHER ---
 *   # Terminal 1:
 *   sudo chrt -r 1 ./loop-bench 5 0 RR-1
 *   # Terminal 2:
 *   sudo chrt -r 1 ./loop-bench 5 0 RR-2
 *   # Terminal 3:
 *   ./loop-bench 5 0 OTHER
 *
 *   The two RR (Round Robin) processes share the CPU equally among
 *   themselves (time-sliced), but the OTHER process is starved.
 *
 *   # --- Same scheduler class, equal priority ---
 *   # Terminal 1:
 *   ./loop-bench 5 0 A
 *   # Terminal 2:
 *   ./loop-bench 5 0 B
 *
 *   Both get roughly equal CPU time (~50% each) under CFS.
 *
 * Background -- Linux scheduler classes (in priority order):
 *
 *   SCHED_FIFO    Real-time, first-in-first-out.  A FIFO task runs
 *                 until it blocks or a higher-priority RT task arrives.
 *                 It NEVER yields to SCHED_OTHER tasks.
 *
 *   SCHED_RR      Real-time, round-robin.  Like FIFO, but tasks at
 *                 the same priority are time-sliced (default quantum
 *                 ~100ms).  Still starves SCHED_OTHER.
 *
 *   SCHED_OTHER   The default (CFS).  Uses nice values (-20..+19) to
 *     (SCHED_NORMAL) weight CPU shares proportionally.
 *
 *   Use chrt(1) to change the scheduler class:
 *     chrt -f <prio> <cmd>    SCHED_FIFO  (prio 1-99)
 *     chrt -r <prio> <cmd>    SCHED_RR    (prio 1-99)
 *     chrt -o 0 <cmd>         SCHED_OTHER (prio always 0)
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sched.h>
#include <time.h>
#include <sys/resource.h>

/* ------------------------------------------------------------------ */
/* Get wall-clock time in seconds (monotonic)                         */
/* ------------------------------------------------------------------ */
static double now_monotonic(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* ------------------------------------------------------------------ */
/* Get process CPU time in seconds                                    */
/* ------------------------------------------------------------------ */
static double process_cpu_time(void)
{
	struct timespec ts;

	/*
	 * CLOCK_PROCESS_CPUTIME_ID measures the total CPU time consumed
	 * by this process.  Time spent sleeping or waiting for the
	 * scheduler does not count -- only actual execution time.
	 */
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* ------------------------------------------------------------------ */
/* Get the name of the current scheduling policy                      */
/* ------------------------------------------------------------------ */
static const char *sched_policy_name(int policy)
{
	switch (policy) {
	case SCHED_OTHER: return "SCHED_OTHER (CFS)";
	case SCHED_FIFO:  return "SCHED_FIFO";
	case SCHED_RR:    return "SCHED_RR";
#ifdef SCHED_BATCH
	case SCHED_BATCH: return "SCHED_BATCH";
#endif
#ifdef SCHED_IDLE
	case SCHED_IDLE:  return "SCHED_IDLE";
#endif
	default:          return "unknown";
	}
}

/* ================================================================== */
/* main                                                               */
/* ================================================================== */
int main(int argc, char *argv[])
{
	int         run_secs = 5;
	int         cpu      = 0;
	const char *label    = "bench";
	long        iters    = 0;
	double      deadline, start_cpu, end_cpu;
	int         last_pct = -1;

	setbuf(stdout, NULL);

	/* Parse optional arguments */
	if (argc >= 2) run_secs = atoi(argv[1]);
	if (argc >= 3) cpu      = atoi(argv[2]);
	if (argc >= 4) label    = argv[3];

	if (run_secs < 1) {
		fprintf(stderr, "SECONDS must be >= 1\n");
		return 1;
	}

	/* -------------------------------------------------------------- */
	/* Pin to the requested CPU core                                  */
	/* -------------------------------------------------------------- */
	{
		cpu_set_t cpuset;
		CPU_ZERO(&cpuset);
		CPU_SET(cpu, &cpuset);

		if (sched_setaffinity(0, sizeof(cpuset), &cpuset) == -1) {
			perror("sched_setaffinity");
			return 1;
		}
	}

	/* -------------------------------------------------------------- */
	/* Print initial info                                             */
	/* -------------------------------------------------------------- */
	{
		int policy = sched_getscheduler(0);
		struct sched_param sp;
		sched_getparam(0, &sp);

		printf("[%s] PID=%d, CPU=%d, duration=%ds\n",
		       label, getpid(), cpu, run_secs);
		printf("[%s] Scheduler: %s, RT priority: %d, nice: %d\n",
		       label, sched_policy_name(policy),
		       sp.sched_priority, getpriority(0, 0));
		printf("[%s] Running...\n", label);
	}

	/* -------------------------------------------------------------- */
	/* Busy loop with periodic progress                               */
	/* -------------------------------------------------------------- */
	start_cpu = process_cpu_time();
	deadline  = now_monotonic() + run_secs;

	while (1) {
		double now = now_monotonic();
		if (now >= deadline)
			break;

		/*
		 * Print progress every 10% of wall-clock time elapsed.
		 * This lets you watch two instances side by side and see
		 * one racing ahead while the other is starved.
		 */
		int pct = (int)(100.0 * (run_secs - (deadline - now)) / run_secs);
		pct = pct / 10 * 10;  /* round down to nearest 10% */
		if (pct > last_pct && pct < 100) {
			end_cpu = process_cpu_time() - start_cpu;
			printf("[%s]   %3d%%  iters=%-12ld  cpu=%.3fs\n",
			       label, pct, iters, end_cpu);
			last_pct = pct;
		}

		iters++;
	}

	end_cpu = process_cpu_time() - start_cpu;

	/* -------------------------------------------------------------- */
	/* Final report                                                   */
	/* -------------------------------------------------------------- */
	printf("[%s]   100%%  iters=%-12ld  cpu=%.3fs\n",
	       label, iters, end_cpu);
	printf("[%s] Done. Total: %ld iterations, %.3f / %d.000 s CPU time",
	       label, iters, end_cpu, run_secs);

	if (run_secs > 0) {
		printf(" (%.1f%%)", 100.0 * end_cpu / run_secs);
	}
	printf("\n");

	return 0;
}
