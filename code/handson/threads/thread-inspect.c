/*
 * thread-inspect.c
 *
 * Spawns a configurable number of threads that each sleep for a
 * configurable duration.  The program prints its PID and useful
 * commands so students can explore threads from another terminal
 * using standard Linux CLI tools.
 *
 * Each thread sets its own name via prctl(PR_SET_NAME), making it
 * easy to identify individual threads in ps/top/htop output.
 *
 * Build:
 *   gcc -Wall -pthread -o thread-inspect thread-inspect.c
 *
 * Usage:
 *   ./thread-inspect [NTHREADS] [SLEEP_SECS]
 *
 *   NTHREADS   - number of threads to create (default: 4)
 *   SLEEP_SECS - how long each thread sleeps (default: 60)
 *
 * Example:
 *   ./thread-inspect 6 120
 *
 * Then, from another terminal, try the commands printed by the program.
 */

/* Required for gettid() wrapper -- available since glibc 2.30 */
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/syscall.h>

/*
 * gettid() was added to glibc in version 2.30.  On older versions
 * we must invoke the system call directly via syscall(2).
 * __GLIBC__ and __GLIBC_MINOR__ are predefined macros that glibc
 * sets to its major and minor version numbers.
 */
#if !defined(__GLIBC__) || \
    (__GLIBC__ < 2) || (__GLIBC__ == 2 && __GLIBC_MINOR__ < 30)
static pid_t gettid(void)
{
	return syscall(SYS_gettid);
}
#endif

/* Default values if no arguments are given */
#define DEFAULT_NTHREADS   4
#define DEFAULT_SLEEP_SECS 60

/* Maximum threads we allow (just a sanity bound) */
#define MAX_THREADS 64

/* ------------------------------------------------------------------ */
/* Thread function                                                    */
/*                                                                    */
/* Each thread:                                                       */
/*   1. Retrieves its kernel thread ID (TID) via gettid()             */
/*   2. Sets a human-readable name via prctl(PR_SET_NAME)             */
/*   3. Sleeps for the requested duration                             */
/*                                                                    */
/* The TID is the identifier the kernel uses to schedule threads.     */
/* Every thread in a process has its own unique TID.  The "main"      */
/* thread's TID equals the process PID.                               */
/* ------------------------------------------------------------------ */

struct thread_arg {
	int id;            /* our thread number (0, 1, 2, ...) */
	pthread_t pthread_tid;   /* pthread thread id from pthread_create() */
	int sleep_secs;    /* how many seconds to sleep        */
};

static void *worker_fn(void *arg)
{
	struct thread_arg *ta = (struct thread_arg *)arg;
	char name[16];  /* prctl name limit: 15 chars + NUL */

	/*
	 * gettid() returns the kernel thread ID (TID).
	 *
	 * This is the same number visible under /proc/<PID>/task/<TID>
	 * and in the LWP / TID columns of ps and top.
	 *
	 * Note: the glibc wrapper for gettid() was added in glibc 2.30.
	 * On older systems we use our own wrapper (defined above) that
	 * calls syscall(SYS_gettid) directly.
	 */
	pid_t tid = gettid();

	/*
	 * Give this thread a descriptive name.
	 *
	 * prctl(PR_SET_NAME) sets the "comm" for this specific thread,
	 * visible in /proc/<PID>/task/<TID>/comm and in tools like
	 * ps -L, top -H, and htop.
	 */
	snprintf(name, sizeof(name), "worker-%d", ta->id);
	prctl(PR_SET_NAME, name);

	/*
	 * getpid() returns the same PID for ALL threads in a process.
	 * This is because threads are not separate processes -- they
	 * share the same address space and the same PID.
	 *
	 * getppid() returns the PID of the parent process (typically
	 * the shell that launched us).  Again, same for all threads.
	 *
	 * Only the TID (gettid()) is unique per thread.  The main
	 * thread's TID equals the PID; all other threads get distinct
	 * TIDs allocated by the kernel.
	 */
	pid_t pid  = getpid();
	pid_t ppid = getppid();

	printf("  [Thread %d] PID=%d, PPID=%d, TID=%d, Pthread TID=%d, name=\"%s\","
	       " sleeping %ds...\n",
	       ta->id, pid, ppid, tid, (int)ta->pthread_tid, name, ta->sleep_secs);

	sleep(ta->sleep_secs);

	printf("  [Thread %d] Woke up, exiting.\n", ta->id);
	return NULL;
}

/* ================================================================== */
/* main                                                               */
/* ================================================================== */
int main(int argc, char *argv[])
{
	int nthreads   = DEFAULT_NTHREADS;
	int sleep_secs = DEFAULT_SLEEP_SECS;
	pid_t pid      = getpid();

	/* Parse optional command-line arguments */
	if (argc >= 2)
		nthreads = atoi(argv[1]);
	if (argc >= 3)
		sleep_secs = atoi(argv[2]);

	if (nthreads < 1 || nthreads > MAX_THREADS) {
		fprintf(stderr, "NTHREADS must be between 1 and %d\n",
			MAX_THREADS);
		return 1;
	}
	if (sleep_secs < 1) {
		fprintf(stderr, "SLEEP_SECS must be >= 1\n");
		return 1;
	}

	printf("=== Thread Inspection Demo ===\n");
	printf("  PID        : %d\n", pid);
	printf("  PPID       : %d\n", getppid());
	printf("  Main TID   : %d (same as PID for the main thread)\n",
	       gettid());
	printf("  Threads    : %d (+ main thread)\n", nthreads);
	printf("  Sleep time : %d seconds\n\n", sleep_secs);

	/* -------------------------------------------------------------- */
	/* Print useful commands the student can run from another terminal */
	/*                                                                */
	/* These tools all show per-thread information when given the      */
	/* right flags:                                                   */
	/*                                                                */
	/*   ps -L       : show LWP (Light Weight Process = thread) IDs   */
	/*   ps -T       : similar, shows SPID column                     */
	/*   top -H -p   : per-thread view in top                         */
	/*   /proc/task/ : one subdirectory per thread (named by TID)     */
	/* -------------------------------------------------------------- */
	printf("Try these commands from another terminal:\n\n");

	printf("  # List all threads with their TIDs and names:\n");
	printf("  ps -p %d -o pid,lwp,nlwp,comm,args\n\n", pid);

	printf("  # Same info, different format (-T shows SPID):\n");
	printf("  ps -p %d -o pid,spid,comm,args\n\n", pid);

	printf("  # Per-thread view in top (press 'q' to quit):\n");
	printf("  top -H -p %d\n\n", pid);

	printf("  # List thread TIDs via /proc (one dir per thread):\n");
	printf("  ls /proc/%d/task/\n\n", pid);

	printf("  # Show each thread's name (comm):\n");
	printf("  for tid in /proc/%d/task/*/; do\n", pid);
	printf("      echo \"TID $(basename $tid): "
	       "$(cat $tid/comm)\";\n");
	printf("  done\n\n");

	printf("  # Show each thread's status (state, voluntary context switches, etc.):\n");
	printf("  cat /proc/%d/task/*/status | grep -E '^(Name|Pid|Tgid|State|"
	       "voluntary)'\n\n", pid);

	printf("──────────────────────────────────────────\n");
	printf("Spawning threads...\n\n");

	/* -------------------------------------------------------------- */
	/* Create the worker threads                                      */
	/* -------------------------------------------------------------- */
	pthread_t         threads[MAX_THREADS];
	struct thread_arg args[MAX_THREADS];

	for (int i = 0; i < nthreads; i++) {
		args[i].id         = i;
		args[i].sleep_secs = sleep_secs;

		int ret = pthread_create(&args[i].pthread_tid, NULL, worker_fn,
					 &args[i]);
		if (ret != 0) {
			fprintf(stderr, "pthread_create: %s\n", strerror(ret));
			return 1;
		}
		threads[i] = args[i].pthread_tid;
	}

	/* -------------------------------------------------------------- */
	/* Wait for all threads to finish                                 */
	/* -------------------------------------------------------------- */
	printf("\n[Main] Waiting for all threads to finish...\n");
	printf("[Main] (You have ~%d seconds to inspect them.)\n\n", sleep_secs);

	for (int i = 0; i < nthreads; i++)
		pthread_join(threads[i], NULL);

	printf("\n[Main] All threads finished.\n");
	printf("Done.\n");

	return 0;
}
