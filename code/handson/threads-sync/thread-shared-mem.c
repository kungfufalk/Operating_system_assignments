/*
 * thread-shared-mem.c
 *
 * Demonstrates that threads within the same process share a common
 * address space.  Unlike separate processes (created via fork()), all
 * threads can directly read and write the same global variables, heap
 * allocations, and any other memory that belongs to the process.
 *
 * The program creates two threads:
 *   - A "writer" thread that fills a shared buffer with data.
 *   - A "reader" thread that reads and prints data from the same buffer.
 *
 * A mutex and a condition variable are used to coordinate the two
 * threads, so the reader waits until the writer has finished before
 * it tries to read.
 *
 * The program also shows what is NOT shared between threads: each
 * thread has its own stack, so local variables are private.
 *
 * Build:
 *   gcc -Wall -pthread -o thread-shared-mem thread-shared-mem.c
 *
 * Run:
 *   ./thread-shared-mem
 */

/* Required for gettid() wrapper -- available since glibc 2.30 */
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
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

/* ------------------------------------------------------------------ */
/* Shared state: accessible by ALL threads in this process            */
/*                                                                    */
/* These variables live in the process's data segment (global memory).*/
/* Every thread sees the same copy -- there is NO per-thread copy.    */
/* This is the fundamental difference between threads and processes:  */
/* fork() gives the child its own COPY of all memory, but             */
/* pthread_create() does not -- threads share everything.             */
/* ------------------------------------------------------------------ */

#define BUF_SIZE 256

/* The shared buffer that the writer fills and the reader consumes */
static char shared_buffer[BUF_SIZE];

/*
 * A flag that the writer sets to 1 when the buffer is ready.
 * The reader waits for this flag before reading.
 *
 * NOTE: We protect this flag with a mutex and signal it with a
 * condition variable.  Using a bare flag without synchronization
 * would be a DATA RACE -- undefined behavior in C.
 */
static int buffer_ready = 0;

/*
 * A mutex (mutual exclusion lock) protects shared state from
 * concurrent access.  Before reading or writing shared variables,
 * a thread must hold the mutex.
 *
 * PTHREAD_MUTEX_INITIALIZER is a convenient way to statically
 * initialize a mutex without calling pthread_mutex_init().
 */
static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;

/*
 * A condition variable allows a thread to sleep until some condition
 * becomes true, without busy-waiting (spinning in a loop).
 *
 * The pattern is:
 *   lock(mutex)
 *   while (!condition)
 *       cond_wait(cond, mutex)   // atomically unlocks mutex & sleeps
 *   // condition is now true, and we hold the mutex
 *   unlock(mutex)
 */
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

/* ------------------------------------------------------------------ */
/* Writer thread function                                             */
/*                                                                    */
/* pthread_create() calls this function in a new thread.  The         */
/* function signature must be: void *(*)(void *).                     */
/* The void* argument can carry any data from the creator thread.     */
/* ------------------------------------------------------------------ */
static void *writer_fn(void *arg)
{
	int thread_num = *(int *)arg;

	/*
	 * This local variable lives on the WRITER's own stack.
	 * The reader thread cannot see it (unless we pass a pointer).
	 */
	int my_local = 42;

	printf("[Writer #%d] Started (tid=%ld)\n", thread_num,
	       (long)gettid());
	printf("[Writer #%d] My stack-local variable is at %p (value=%d)\n",
	       thread_num, (void *)&my_local, my_local);

	/*
	 * Write a message into the shared buffer.
	 *
	 * We first lock the mutex to ensure exclusive access -- even
	 * though the reader should be waiting, it is good practice to
	 * always protect shared data with a lock.
	 */
	pthread_mutex_lock(&mtx);

	snprintf(shared_buffer, BUF_SIZE,
		 "Hello from writer thread #%d (tid=%ld)!",
		 thread_num, (long)gettid());

	printf("[Writer #%d] Wrote to shared_buffer at %p: \"%s\"\n",
	       thread_num, (void *)shared_buffer, shared_buffer);

	/*
	 * Signal the reader: set the flag and wake it up.
	 *
	 * pthread_cond_signal() wakes ONE thread that is currently
	 * blocked in pthread_cond_wait() on this condition variable.
	 * The woken thread will re-acquire the mutex before returning
	 * from cond_wait.
	 */
	buffer_ready = 1;
	pthread_cond_signal(&cond);

	pthread_mutex_unlock(&mtx);

	printf("[Writer #%d] Done, exiting.\n", thread_num);
	return NULL;
}

/* ------------------------------------------------------------------ */
/* Reader thread function                                             */
/* ------------------------------------------------------------------ */
static void *reader_fn(void *arg)
{
	int thread_num = *(int *)arg;

	/*
	 * This local variable is on the READER's own stack -- distinct
	 * from the writer's stack.  Each thread gets its own stack
	 * (typically 8 MB by default on Linux, see `ulimit -s`).
	 */
	int my_local = 99;

	printf("[Reader #%d] Started (tid=%ld)\n", thread_num,
	       (long)gettid());
	printf("[Reader #%d] My stack-local variable is at %p (value=%d)\n",
	       thread_num, (void *)&my_local, my_local);

	/*
	 * Wait until the writer has filled the buffer.
	 *
	 * The idiom is:
	 *   lock mutex
	 *   while (condition is false)
	 *       wait on condition variable (releases mutex while sleeping)
	 *   ... use the shared data ...
	 *   unlock mutex
	 *
	 * We use a while-loop (not an if) because of "spurious wakeups":
	 * pthread_cond_wait() is allowed to return even when nobody
	 * called pthread_cond_signal().  The while-loop handles this by
	 * re-checking the condition.
	 */
	pthread_mutex_lock(&mtx);

	while (!buffer_ready) {
		printf("[Reader #%d] Buffer not ready, waiting...\n",
		       thread_num);
		pthread_cond_wait(&cond, &mtx);
	}

	/*
	 * We now hold the mutex and buffer_ready == 1.
	 * Read from the SAME shared_buffer that the writer wrote to.
	 * No copy was made -- this is the exact same memory.
	 */
	printf("[Reader #%d] Read from shared_buffer at %p: \"%s\"\n",
	       thread_num, (void *)shared_buffer, shared_buffer);

	pthread_mutex_unlock(&mtx);

	printf("[Reader #%d] Done, exiting.\n", thread_num);
	return NULL;
}

/* ================================================================== */
/* main                                                               */
/* ================================================================== */
int main(void)
{
	pthread_t writer_thread, reader_thread;
	int writer_id = 1, reader_id = 2;
	int ret;

	printf("=== Thread Shared Memory Demo ===\n");
	printf("  Main thread tid = %ld, pid = %d\n\n",
	       (long)gettid(), getpid());

	/* Show where shared data lives before any thread starts */
	printf("  shared_buffer address: %p (global / data segment)\n",
	       (void *)shared_buffer);
	printf("  buffer_ready address : %p (global / data segment)\n",
	       (void *)&buffer_ready);
	printf("  mutex address        : %p (global / data segment)\n",
	       (void *)&mtx);
	printf("\n");

	/* -------------------------------------------------------------- */
	/* Create the reader thread FIRST.                                */
	/*                                                                */
	/* It will block on the condition variable until the writer        */
	/* signals that data is ready.  This shows that synchronization   */
	/* works regardless of which thread starts first.                 */
	/*                                                                */
	/* pthread_create() arguments:                                    */
	/*   1. &thread  -- output: the new thread's ID                   */
	/*   2. NULL     -- thread attributes (NULL = defaults)           */
	/*   3. func     -- the function to run in the new thread         */
	/*   4. arg      -- a void* argument passed to that function      */
	/* -------------------------------------------------------------- */
	printf("[Main] Creating reader thread...\n");
	ret = pthread_create(&reader_thread, NULL, reader_fn, &reader_id);
	if (ret != 0) {
		fprintf(stderr, "pthread_create(reader): %s\n", strerror(ret));
		return 1;
	}

	/*
	 * Small sleep so the reader has time to print its "waiting..."
	 * message before the writer starts.  This is just for cleaner
	 * demo output -- in real code you should NOT rely on sleep for
	 * synchronization.
	 */
	usleep(100000);  /* 100 ms */

	printf("[Main] Creating writer thread...\n");
	ret = pthread_create(&writer_thread, NULL, writer_fn, &writer_id);
	if (ret != 0) {
		fprintf(stderr, "pthread_create(writer): %s\n", strerror(ret));
		return 1;
	}

	/* -------------------------------------------------------------- */
	/* Wait for both threads to finish.                               */
	/*                                                                */
	/* pthread_join() blocks the calling thread until the target      */
	/* thread terminates.  It also cleans up the thread's resources.  */
	/* If we did not join, the thread's resources would leak (similar */
	/* to a zombie process that is never wait()'d for).               */
	/*                                                                */
	/* The second argument can receive the thread's return value      */
	/* (the void* returned by the thread function).  We pass NULL     */
	/* because we don't need it here.                                 */
	/* -------------------------------------------------------------- */
	pthread_join(writer_thread, NULL);
	pthread_join(reader_thread, NULL);

	/* -------------------------------------------------------------- */
	/* Show that the shared buffer still holds the writer's data.     */
	/*                                                                */
	/* The buffer persists because it is a global variable -- it      */
	/* belongs to the process, not to any individual thread.  The     */
	/* writer thread is gone, but its writes to shared memory remain. */
	/* -------------------------------------------------------------- */
	printf("\n[Main] After both threads finished:\n");
	printf("  shared_buffer still contains: \"%s\"\n", shared_buffer);

	/* -------------------------------------------------------------- */
	/* Summary                                                        */
	/* -------------------------------------------------------------- */
	printf("\n=== Summary ===\n");
	printf("  - Threads share: global variables, heap, file descriptors\n");
	printf("  - Threads do NOT share: stack (local variables)\n");
	printf("  - Access to shared data MUST be synchronized (mutex, etc.)\n");
	printf("  - Condition variables let threads sleep/wake efficiently\n");
	printf("\nDone.\n");

	return 0;
}
