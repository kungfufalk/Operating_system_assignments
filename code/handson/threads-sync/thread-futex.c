/*
 * thread-futex.c
 *
 * Demonstrates using the futex(2) system call directly for a simple
 * waiting/notification mechanism between threads.
 *
 * A "futex" (Fast Userspace muTEX) is the fundamental building block
 * that the kernel provides for synchronization.  Higher-level
 * primitives like pthread_mutex_lock(), pthread_cond_wait(), and
 * sem_wait() are all built ON TOP of futex internally.
 *
 * The futex API operates on a plain integer in user memory:
 *
 *   FUTEX_WAIT(addr, expected_val):
 *     "If *addr == expected_val, put me to sleep until someone
 *      calls FUTEX_WAKE on this address."
 *     If *addr != expected_val, return immediately (avoids a race
 *     between checking the value and going to sleep).
 *
 *   FUTEX_WAKE(addr, num_to_wake):
 *     "Wake up to num_to_wake threads sleeping on this address."
 *
 * The key insight is that the CHECK and SLEEP in FUTEX_WAIT happen
 * ATOMICALLY inside the kernel -- there is no window where another
 * thread could change the value and call WAKE between our check and
 * our sleep, which would cause us to miss the wakeup.
 *
 * This program uses a raw futex as a simple event / notification:
 *   - A waiter thread sleeps until a shared variable changes.
 *   - A notifier thread changes the variable and wakes the waiter.
 *
 * Build:
 *   gcc -Wall -pthread -o thread-futex thread-futex.c
 *
 * Run:
 *   ./thread-futex
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <stdatomic.h>
#include <errno.h>
#include <sys/syscall.h>
#include <linux/futex.h>

/* ------------------------------------------------------------------ */
/* Futex wrapper functions                                            */
/*                                                                    */
/* There is no glibc wrapper for futex(2) -- we must call it via      */
/* syscall(2) directly.  This is the raw kernel interface.            */
/* ------------------------------------------------------------------ */

/*
 * futex_wait -- sleep if *addr still equals expected.
 *
 * Returns 0 on successful wakeup, -1 on error.
 * Common error: EAGAIN means *addr != expected (value already changed,
 * so there is nothing to wait for).
 */
static int futex_wait(atomic_int *addr, int expected)
{
	/*
	 * syscall(SYS_futex, ...) arguments:
	 *   arg1: pointer to the futex word (the integer in user memory)
	 *   arg2: operation (FUTEX_WAIT = sleep)
	 *   arg3: expected value (sleep only if *addr == this)
	 *   arg4: timeout (NULL = wait forever)
	 */
	return syscall(SYS_futex, addr, FUTEX_WAIT, expected,
		       NULL /* no timeout */);
}

/*
 * futex_wake -- wake up to 'count' threads sleeping on *addr.
 *
 * Returns the number of threads actually woken, or -1 on error.
 * Passing count=1 wakes one waiter, count=INT_MAX wakes all.
 */
static int futex_wake(atomic_int *addr, int count)
{
	/*
	 * syscall(SYS_futex, ...) arguments:
	 *   arg1: pointer to the futex word
	 *   arg2: operation (FUTEX_WAKE = wake)
	 *   arg3: max number of threads to wake
	 */
	return syscall(SYS_futex, addr, FUTEX_WAKE, count);
}

/* ------------------------------------------------------------------ */
/* Shared futex word                                                  */
/*                                                                    */
/* This is just a plain integer in shared memory.  The kernel does    */
/* not store any state for it -- the futex mechanism works entirely   */
/* based on the ADDRESS of this variable and its current VALUE.       */
/*                                                                    */
/* We use atomic_int (C11 atomics) so that the compiler does not     */
/* optimize away or reorder our reads and writes.  The futex syscall  */
/* itself provides the kernel-side ordering, but we still need the    */
/* compiler to actually emit the loads and stores.                    */
/* ------------------------------------------------------------------ */

#define STATE_WAITING  0   /* waiter should sleep (initial state) */
#define STATE_READY    1   /* notifier has signaled, waiter can proceed */

static atomic_int futex_var = STATE_WAITING;

/* ------------------------------------------------------------------ */
/* Waiter thread                                                      */
/*                                                                    */
/* Sleeps on the futex until the notifier changes the value and       */
/* wakes us up.                                                       */
/* ------------------------------------------------------------------ */
static void *waiter_fn(void *arg)
{
	(void)arg;

	printf("[Waiter]   Started. Waiting for notification...\n");

	/*
	 * Loop around FUTEX_WAIT.  We need a loop because:
	 *
	 *   1. Spurious wakeups: the kernel is allowed to wake us even
	 *      when nobody called FUTEX_WAKE (same as with condition
	 *      variables -- this is a fundamental property).
	 *
	 *   2. EAGAIN: if the notifier changed the value AND called
	 *      FUTEX_WAKE before we entered FUTEX_WAIT, the syscall
	 *      returns immediately with EAGAIN because
	 *      *addr != expected.  This is NOT an error -- it means
	 *      the event already happened.
	 *
	 * In both cases, we re-check the actual value to decide
	 * whether to keep waiting or proceed.
	 */
	while (atomic_load(&futex_var) == STATE_WAITING) {
		printf("[Waiter]   Calling futex(FUTEX_WAIT, expected=%d)...\n",
		       STATE_WAITING);

		int ret = futex_wait(&futex_var, STATE_WAITING);

		if (ret == -1 && errno == EAGAIN) {
			/*
			 * The value already changed before we entered the
			 * kernel.  The loop condition will catch this.
			 */
			printf("[Waiter]   FUTEX_WAIT returned EAGAIN "
			       "(value already changed).\n");
		} else if (ret == -1) {
			perror("[Waiter]   futex(FUTEX_WAIT)");
		} else {
			printf("[Waiter]   Woke up from FUTEX_WAIT!\n");
		}
	}

	printf("[Waiter]   futex_var is now %d (STATE_READY) -- proceeding.\n",
	       atomic_load(&futex_var));
	printf("[Waiter]   Done.\n");
	return NULL;
}

/* ------------------------------------------------------------------ */
/* Notifier thread                                                    */
/*                                                                    */
/* Sleeps for a couple of seconds (simulating work), then changes the */
/* futex variable and wakes the waiter.                               */
/* ------------------------------------------------------------------ */
static void *notifier_fn(void *arg)
{
	(void)arg;

	printf("[Notifier] Started. Doing some work...\n");
	sleep(2);

	/*
	 * Step 1: Change the value.
	 *
	 * We MUST change the value BEFORE calling FUTEX_WAKE.
	 * Otherwise the waiter would wake up, see the old value,
	 * and go right back to sleep.
	 */
	printf("[Notifier] Setting futex_var = STATE_READY.\n");
	atomic_store(&futex_var, STATE_READY);

	/*
	 * Step 2: Wake the waiter.
	 *
	 * FUTEX_WAKE with count=1 wakes one sleeping thread.
	 * If the waiter hasn't called FUTEX_WAIT yet (unlikely here
	 * because of the 2-second sleep), this is harmless -- the wake
	 * just has no effect, and the waiter's next FUTEX_WAIT will
	 * return EAGAIN because the value already changed.
	 */
	printf("[Notifier] Calling futex(FUTEX_WAKE, count=1)...\n");
	int woken = futex_wake(&futex_var, 1);
	printf("[Notifier] FUTEX_WAKE returned %d (threads woken).\n", woken);

	printf("[Notifier] Done.\n");
	return NULL;
}

/* ================================================================== */
/* main                                                               */
/* ================================================================== */
int main(void)
{
	pthread_t waiter, notifier;
	int ret;

	setbuf(stdout, NULL);

	printf("=== Futex Demo ===\n\n");
	printf("  futex_var address: %p\n", (void *)&futex_var);
	printf("  initial value    : %d (STATE_WAITING)\n\n", STATE_WAITING);
	printf("  The waiter thread will call futex(FUTEX_WAIT) to sleep.\n");
	printf("  The notifier thread will change the value, then call\n");
	printf("  futex(FUTEX_WAKE) to wake the waiter.\n\n");
	printf("──────────────────────────────────────────\n\n");

	/* Start the waiter first so it is sleeping when notifier fires */
	ret = pthread_create(&waiter, NULL, waiter_fn, NULL);
	if (ret != 0) {
		fprintf(stderr, "pthread_create(waiter): %s\n", strerror(ret));
		return 1;
	}

	/* Small delay so the waiter prints its messages first */
	usleep(100000);

	ret = pthread_create(&notifier, NULL, notifier_fn, NULL);
	if (ret != 0) {
		fprintf(stderr, "pthread_create(notifier): %s\n", strerror(ret));
		return 1;
	}

	pthread_join(waiter, NULL);
	pthread_join(notifier, NULL);

	printf("\n=== Summary ===\n");
	printf("  futex(FUTEX_WAIT, val):\n");
	printf("    If *addr == val, sleep until woken. Atomic check+sleep.\n");
	printf("  futex(FUTEX_WAKE, n):\n");
	printf("    Wake up to n threads sleeping on this address.\n");
	printf("  This is how pthread_mutex, pthread_cond, sem_t, etc.\n");
	printf("  are implemented under the hood.\n");
	printf("\nDone.\n");

	return 0;
}
