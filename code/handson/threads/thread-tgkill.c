/*
 * thread-tgkill.c
 *
 * Demonstrates using the tgkill(2) system call to deliver signals to
 * individual threads, rather than to the process as a whole.
 *
 * Background -- signals and threads on Linux:
 *
 *   kill(pid, sig)          sends a signal to the PROCESS.  The kernel
 *                           picks an arbitrary thread that has not blocked
 *                           the signal to handle it.  You cannot control
 *                           which thread receives it.
 *
 *   tgkill(tgid, tid, sig) sends a signal to a SPECIFIC THREAD identified
 *                           by its TID, within the thread group (process)
 *                           identified by TGID (= PID).  Only that thread
 *                           will handle the signal.
 *
 *   The "tg" in tgkill stands for "thread group" -- the kernel's name
 *   for what userspace calls a process.  TGID = Thread Group ID = PID.
 *
 *   pthread_kill(thread, sig) is the POSIX wrapper that does roughly
 *   the same thing as tgkill, but takes a pthread_t instead of a raw
 *   TID.  Internally, glibc implements pthread_kill() using tgkill().
 *
 * The program:
 *   1. Creates several worker threads, each of which registers a
 *      signal handler for SIGUSR1 and then sleeps.
 *   2. The main thread uses tgkill() to send SIGUSR1 to each worker
 *      thread one at a time, showing that only the targeted thread
 *      handles the signal.
 *   3. Then sends SIGUSR2 via kill() (process-wide) to show the
 *      contrast: any thread may handle it.
 *
 * Build:
 *   gcc -Wall -pthread -o thread-tgkill thread-tgkill.c
 *
 * Run:
 *   ./thread-tgkill
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/syscall.h>

/*
 * gettid() was added to glibc in version 2.30.  On older versions
 * we must invoke the system call directly via syscall(2).
 */
#if !defined(__GLIBC__) || \
    (__GLIBC__ < 2) || (__GLIBC__ == 2 && __GLIBC_MINOR__ < 30)
static pid_t gettid(void)
{
	return syscall(SYS_gettid);
}
#endif

#define NTHREADS 4

/* ------------------------------------------------------------------ */
/* tgkill wrapper                                                     */
/*                                                                    */
/* Like gettid(), the glibc wrapper for tgkill() was added in         */
/* glibc 2.30.  On older versions we call the system call directly.   */
/*                                                                    */
/* Arguments:                                                         */
/*   tgid - thread group ID (= PID of the process)                   */
/*   tid  - thread ID of the specific thread to signal                */
/*   sig  - signal number to deliver                                  */
/*                                                                    */
/* The tgid argument exists to prevent a race: if a thread exits and  */
/* its TID is reused by a thread in a DIFFERENT process, tgid ensures */
/* we don't accidentally signal the wrong process.                    */
/* ------------------------------------------------------------------ */
#if !defined(__GLIBC__) || \
    (__GLIBC__ < 2) || (__GLIBC__ == 2 && __GLIBC_MINOR__ < 30)
static int tgkill(pid_t tgid, pid_t tid, int sig)
{
	return syscall(SYS_tgkill, tgid, tid, sig);
}
#endif

/* ------------------------------------------------------------------ */
/* Shared state: each worker records its TID here so the main thread  */
/* knows which TIDs to target with tgkill().                          */
/* ------------------------------------------------------------------ */
static pid_t worker_tids[NTHREADS];

/*
 * A barrier ensures all threads have registered their signal handlers
 * and recorded their TIDs before the main thread starts sending
 * signals.
 */
static pthread_barrier_t barrier;

/* ------------------------------------------------------------------ */
/* Signal handlers                                                    */
/*                                                                    */
/* Signal handlers are installed per-PROCESS, not per-thread.  All    */
/* threads share the same set of signal dispositions (handlers).      */
/* What tgkill controls is which thread RECEIVES the signal, not      */
/* which handler runs -- the handler is always the same.              */
/*                                                                    */
/* IMPORTANT: signal handlers run in a restricted context.  Only      */
/* async-signal-safe functions may be called (see signal-safety(7)).  */
/* printf() is NOT async-signal-safe, but we use it here for clarity  */
/* in this educational demo.  In production code, use write(2).       */
/* ------------------------------------------------------------------ */
static void sigusr1_handler(int sig)
{
	(void)sig;
	printf("    >> SIGUSR1 received by TID=%d\n", gettid());
}

static void sigusr2_handler(int sig)
{
	(void)sig;
	printf("    >> SIGUSR2 received by TID=%d\n", gettid());
}

/* ------------------------------------------------------------------ */
/* Worker thread function                                             */
/* ------------------------------------------------------------------ */
static void *worker_fn(void *arg)
{
	int id = *(int *)arg;

	/* Record our TID so the main thread can target us */
	worker_tids[id] = gettid();

	printf("  [Worker %d] TID=%d, ready.\n", id, worker_tids[id]);

	/*
	 * Wait at the barrier until all workers (and main) are ready.
	 * This ensures no signal is sent before every thread has
	 * registered its TID.
	 */
	pthread_barrier_wait(&barrier);

	/*
	 * Sleep in a loop.  We use a loop because sleep() returns
	 * early when interrupted by a signal -- the remaining time
	 * is returned so we can resume sleeping.
	 */
	int remaining = 10;
	while (remaining > 0)
		remaining = sleep(remaining);

	printf("  [Worker %d] Exiting.\n", id);
	return NULL;
}

/* ================================================================== */
/* main                                                               */
/* ================================================================== */
int main(void)
{
	pthread_t threads[NTHREADS];
	int ids[NTHREADS];
	pid_t     pid = getpid();
	int       ret;

	setbuf(stdout, NULL);

	printf("=== tgkill Demo ===\n");
	printf("  PID (TGID): %d\n", pid);
	printf("  Main TID  : %d\n\n", gettid());

	/* -------------------------------------------------------------- */
	/* Install signal handlers (process-wide)                         */
	/*                                                                */
	/* sigaction() is preferred over signal() because its behavior is */
	/* well-defined across systems.  SA_RESTART makes interrupted     */
	/* system calls (like sleep) restart automatically where possible.*/
	/* -------------------------------------------------------------- */
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sigusr1_handler;
	sa.sa_flags   = SA_RESTART;
	if (sigaction(SIGUSR1, &sa, NULL) == -1) {
		perror("sigaction(SIGUSR1)");
		return 1;
	}

	sa.sa_handler = sigusr2_handler;
	if (sigaction(SIGUSR2, &sa, NULL) == -1) {
		perror("sigaction(SIGUSR2)");
		return 1;
	}

	/*
	 * Initialize the barrier for NTHREADS workers + 1 main thread.
	 * All must call pthread_barrier_wait() before any can proceed.
	 */
	pthread_barrier_init(&barrier, NULL, NTHREADS + 1);

	/* -------------------------------------------------------------- */
	/* Create worker threads                                          */
	/* -------------------------------------------------------------- */
	printf("Creating %d worker threads...\n\n", NTHREADS);

	for (int i = 0; i < NTHREADS; i++) {
		ids[i] = i;
		ret = pthread_create(&threads[i], NULL, worker_fn, &ids[i]);
		if (ret != 0) {
			fprintf(stderr, "pthread_create: %s\n", strerror(ret));
			return 1;
		}
	}

	/* Wait until all workers have recorded their TIDs */
	pthread_barrier_wait(&barrier);

	/* Small delay for cleaner output */
	usleep(100000);

	/* -------------------------------------------------------------- */
	/* Part 1: tgkill() -- send SIGUSR1 to each thread individually   */
	/*                                                                */
	/* tgkill(tgid, tid, sig) delivers the signal to EXACTLY the      */
	/* thread with the given TID.  No other thread can handle it.     */
	/* -------------------------------------------------------------- */
	printf("──────────────────────────────────────────\n");
	printf("[Part 1] Sending SIGUSR1 to each thread via tgkill()\n\n");

	for (int i = 0; i < NTHREADS; i++) {
		printf("  Sending SIGUSR1 to worker %d (TID=%d)...\n",
		       i, worker_tids[i]);

		ret = tgkill(pid, worker_tids[i], SIGUSR1);
		if (ret == -1) {
			perror("  tgkill");
			continue;
		}

		/*
		 * Small delay so the signal is delivered and the handler
		 * prints its message before we send the next one.
		 */
		usleep(100000);
	}

	/* -------------------------------------------------------------- */
	/* Part 2: kill() -- send SIGUSR2 to the process                  */
	/*                                                                */
	/* kill(pid, sig) sends the signal to the PROCESS.  The kernel    */
	/* chooses an arbitrary thread (that hasn't blocked the signal)   */
	/* to deliver it to.  We send it a few times to show that the     */
	/* receiving thread may vary.                                     */
	/* -------------------------------------------------------------- */
	printf("\n──────────────────────────────────────────\n");
	printf("[Part 2] Sending SIGUSR2 to the process via kill()\n");
	printf("  (the kernel picks which thread handles it)\n\n");

	for (int i = 0; i < NTHREADS; i++) {
		printf("  Sending SIGUSR2 to process (PID=%d)...\n", pid);

		if (kill(pid, SIGUSR2) == -1) {
			perror("  kill");
			continue;
		}

		usleep(100000);
	}

	/* -------------------------------------------------------------- */
	/* Clean up                                                       */
	/* -------------------------------------------------------------- */
	printf("\n──────────────────────────────────────────\n");
	printf("Using pthread_cancel() to terminate worker threads and waiting for them to terminate...\n");

	for (int i = 0; i < NTHREADS; i++)
		pthread_cancel(threads[i]);

	for (int i = 0; i < NTHREADS; i++)
		pthread_join(threads[i], NULL);

	pthread_barrier_destroy(&barrier);

	printf("\n=== Summary ===\n");
	printf("  tgkill(tgid, tid, sig):\n");
	printf("    Sends a signal to a SPECIFIC thread (by TID).\n");
	printf("    Only that thread handles it.\n\n");
	printf("  kill(pid, sig):\n");
	printf("    Sends a signal to the PROCESS.\n");
	printf("    The kernel picks any eligible thread to handle it.\n\n");
	printf("  pthread_kill(thread, sig):\n");
	printf("    POSIX wrapper around tgkill -- same effect,\n");
	printf("    but takes a pthread_t instead of a raw TID.\n");
	printf("\nDone.\n");

	return 0;
}
