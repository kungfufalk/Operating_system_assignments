/*
 * thread-deadlock.c
 *
 * Demonstrates a classic deadlock between two threads.
 *
 * Two threads each need to hold TWO mutexes to do their work, but
 * they acquire them in OPPOSITE order:
 *
 *   Thread A              Thread B
 *   ────────              ────────
 *   lock(mutex_1)         lock(mutex_2)
 *   lock(mutex_2)  ←───→  lock(mutex_1)
 *        ↑                     ↑
 *     BLOCKED              BLOCKED
 *     (B holds it)         (A holds it)
 *
 * Neither thread can proceed because each is waiting for the lock
 * that the other thread already holds.  This is a DEADLOCK -- the
 * program hangs forever.
 *
 * A deadlock requires ALL FOUR of the Coffman conditions:
 *
 *   1. Mutual exclusion  -- only one thread can hold a mutex at a time
 *   2. Hold and wait     -- a thread holds one lock while waiting for another
 *   3. No preemption     -- a mutex cannot be forcibly taken from its holder
 *   4. Circular wait     -- A waits for B, and B waits for A
 *
 * Breaking ANY ONE of these conditions prevents the deadlock.
 * The simplest fix is to break condition 4: always acquire mutexes
 * in the SAME global order (e.g., always lock mutex_1 before mutex_2).
 *
 * Build:
 *   gcc -Wall -pthread -o thread-deadlock thread-deadlock.c
 *
 * Run:
 *   ./thread-deadlock
 *
 * The program will hang.  Kill it with Ctrl-C.
 *
 * To see the stuck threads from another terminal:
 *   ps -p $(pgrep thread-deadl) -L -o pid,lwp,comm,wchan
 *
 * The "wchan" column shows the kernel function each thread is
 * sleeping in -- you will see both threads blocked in futex_wait
 * (the underlying implementation of pthread_mutex_lock).
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/prctl.h>

/* ------------------------------------------------------------------ */
/* Two mutexes that both threads need to acquire                      */
/* ------------------------------------------------------------------ */
static pthread_mutex_t mutex_1 = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t mutex_2 = PTHREAD_MUTEX_INITIALIZER;

/* ------------------------------------------------------------------ */
/* Thread A: acquires mutex_1 first, then mutex_2                     */
/* ------------------------------------------------------------------ */
static void *thread_a_fn(void *arg)
{
	(void)arg;  /* unused */
	prctl(PR_SET_NAME, "deadlock-A");

	printf("[Thread A] Locking mutex_1...\n");
	pthread_mutex_lock(&mutex_1);
	printf("[Thread A] Acquired mutex_1.\n");

	/*
	 * Sleep briefly to make the deadlock reproducible.
	 *
	 * Without the sleep, Thread A might acquire BOTH mutexes before
	 * Thread B even starts, and no deadlock would occur.  In real
	 * programs, deadlocks are timing-dependent and hard to reproduce
	 * -- they happen "randomly" under load, which makes them
	 * particularly nasty bugs.
	 *
	 * The sleep simulates Thread A doing some work while holding
	 * mutex_1, giving Thread B time to acquire mutex_2.
	 */
	printf("[Thread A] Doing some work while holding mutex_1...\n");
	sleep(1);

	printf("[Thread A] Now locking mutex_2...\n");
	/*
	 * THIS BLOCKS FOREVER:
	 *
	 * Thread B already holds mutex_2, and Thread B is blocked
	 * waiting for mutex_1 which we hold.  Neither thread can
	 * make progress -- deadlock.
	 */
	pthread_mutex_lock(&mutex_2);

	/* We never reach this point */
	printf("[Thread A] Acquired both mutexes! (no deadlock)\n");
	pthread_mutex_unlock(&mutex_2);
	pthread_mutex_unlock(&mutex_1);
	return NULL;
}

/* ------------------------------------------------------------------ */
/* Thread B: acquires mutex_2 first, then mutex_1 (OPPOSITE order!)   */
/* ------------------------------------------------------------------ */
static void *thread_b_fn(void *arg)
{
	(void)arg;  /* unused */
	prctl(PR_SET_NAME, "deadlock-B");

	printf("[Thread B] Locking mutex_2...\n");
	pthread_mutex_lock(&mutex_2);
	printf("[Thread B] Acquired mutex_2.\n");

	/* Same sleep as Thread A, to ensure both threads hold one lock */
	printf("[Thread B] Doing some work while holding mutex_2...\n");
	sleep(1);

	printf("[Thread B] Now locking mutex_1...\n");
	/*
	 * THIS BLOCKS FOREVER:
	 *
	 * Thread A already holds mutex_1, and Thread A is blocked
	 * waiting for mutex_2 which we hold.  Circular wait = deadlock.
	 */
	pthread_mutex_lock(&mutex_1);

	/* We never reach this point */
	printf("[Thread B] Acquired both mutexes! (no deadlock)\n");
	pthread_mutex_unlock(&mutex_1);
	pthread_mutex_unlock(&mutex_2);
	return NULL;
}

/* ================================================================== */
/* main                                                               */
/* ================================================================== */
int main(void)
{
	pthread_t ta, tb;
	int ret;
	pid_t pid = getpid();

	/*
	 * Disable stdout buffering so that all printf output is visible
	 * immediately, even if the program is killed mid-deadlock.
	 */
	setbuf(stdout, NULL);

	printf("=== Deadlock Demo ===\n");
	printf("  PID: %d\n\n", pid);

	printf("  Thread A will lock: mutex_1 -> mutex_2\n");
	printf("  Thread B will lock: mutex_2 -> mutex_1  (opposite order!)\n\n");

	printf("Inspect the deadlocked threads from another terminal:\n\n");
	printf("  # Show threads and what kernel function they are blocked in:\n");
	printf("  ps -p %d -L -o pid,lwp,comm,wchan\n\n", pid);
	printf("  # Show full thread state via /proc:\n");
	printf("  cat /proc/%d/task/*/status | "
	       "grep -E '^(Name|Pid|State|voluntary)'\n\n", pid);
	printf("  # Kill the deadlocked process:\n");
	printf("  kill %d\n\n", pid);

	printf("──────────────────────────────────────────\n");
	printf("Starting threads...\n\n");

	/* Create both threads */
	ret = pthread_create(&ta, NULL, thread_a_fn, NULL);
	if (ret != 0) {
		fprintf(stderr, "pthread_create(A): %s\n", strerror(ret));
		return 1;
	}

	ret = pthread_create(&tb, NULL, thread_b_fn, NULL);
	if (ret != 0) {
		fprintf(stderr, "pthread_create(B): %s\n", strerror(ret));
		return 1;
	}

	/*
	 * Wait for both threads.  Since they are deadlocked, these
	 * joins will block forever -- the program hangs here.
	 */
	printf("[Main] Waiting for threads (this will hang!)...\n\n");
	pthread_join(ta, NULL);
	pthread_join(tb, NULL);

	/* We never reach this point */
	printf("[Main] Both threads finished (no deadlock occurred).\n");
	printf("Done.\n");

	return 0;
}
