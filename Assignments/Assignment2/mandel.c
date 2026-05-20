#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <pthread.h>

#include "mandel-lib.h"
#include <signal.h>
#include <semaphore.h>
sem_t *sems;

#define MANDEL_MAX_ITERATION 100000

#define CONDITION_VARIABLES
#if defined(SEMAPHORES) ^ defined(CONDITION_VARIABLES) == 0
#error You must #define exactly one of SEMEPHORES or CONDITION_VARIABLES.
#endif

#if defined(SEMAPHORES)
#define USE_SEMAPHORES 1
#else
#define USE_SEMAPHORES 0
#endif

/***************************
 * Compile-time parameters *
 ***************************/

pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
int next_line = 0;

/*
 * Output at the terminal is is x_chars wide by y_chars long
 */
int y_chars = 50;
int x_chars = 90;

/*
 * The part of the complex plane to be drawn:
 * upper left corner is (xmin, ymax), lower right corner is (xmax, ymin)
 */
double xmin = -1.8, xmax = 1.0;
double ymin = -1.0, ymax = 1.0;

/*
 * Every character in the final output is
 * xstep x ystep units wide on the complex plane.
 */
double xstep;
double ystep;

void handle_sigint(int sig)
{
	reset_xterm_color(1);
	exit(0);
}

typedef struct
{
	int thread_id;
	int nthreads;
} thread_args_t;

/*
 * This function computes a line of output
 * as an array of x_char color values.
 */
void compute_mandel_line(int line, int color_val[])
{
	/*
	 * x and y traverse the complex plane.
	 */
	double x, y;

	int n;
	int val;

	/* Find out the y value corresponding to this line */
	y = ymax - ystep * line;

	/* and iterate for all points on this line */
	for (x = xmin, n = 0; n < x_chars; x += xstep, n++)
	{

		/* Compute the point's color value */
		val = mandel_iterations_at_point(x, y, MANDEL_MAX_ITERATION);
		if (val > 255)
			val = 255;

		/* And store it in the color_val[] array */
		val = xterm_color(val);
		color_val[n] = val;
	}
}

/*
 * This function outputs an array of x_char color values
 * to a 256-color xterm.
 */
void output_mandel_line(int fd, int color_val[])
{
	int i;

	char point = '@';
	char newline = '\n';

	for (i = 0; i < x_chars; i++)
	{
		/* Set the current color, then output the point */
		set_xterm_color(fd, color_val[i]);
		if (write(fd, &point, 1) != 1)
		{
			perror("compute_and_output_mandel_line: write point");
			exit(1);
		}
	}

	/* Now that the line is done, output a newline character */
	if (write(fd, &newline, 1) != 1)
	{
		perror("compute_and_output_mandel_line: write newline");
		exit(1);
	}
}

void compute_and_output_mandel_line(int fd, int line)
{
	/*
	 * A temporary array, used to hold color values for the line being drawn
	 */
	int color_val[x_chars];

	compute_mandel_line(line, color_val);
	output_mandel_line(fd, color_val);
}

void *mandel_thread(void *arg)
{
	thread_args_t *my_args = (thread_args_t *)arg;
	int i = my_args->thread_id;
	int step = my_args->nthreads;

	if (USE_SEMAPHORES)
	{
		for (int line = i; line < y_chars; line += step)
		{
			sem_wait(&sems[i]);
			compute_and_output_mandel_line(1, line);
			sem_post(&sems[(i + 1) % step]);
		}
	}

	else
	{
		for (int line = i; line < y_chars; line += step)
		{
			pthread_mutex_lock(&lock);
			while (next_line != line)
			{
				pthread_cond_wait(&cond, &lock);
			}

			compute_and_output_mandel_line(1, line);
			next_line++;
			pthread_cond_broadcast(&cond);
			pthread_mutex_unlock(&lock);
		}
	}
	return NULL;
}

int main(int argc, char *argv[])
{
	signal(SIGINT, handle_sigint);
	int nthreads;
	if (argc < 2)
	{
		fprintf(stderr, "Usage: %s <nthreads>\n", argv[0]);
		exit(1);
	}

	nthreads = atoi(argv[1]);
	pthread_t *threads = malloc(sizeof(pthread_t) * nthreads);
	thread_args_t *args = malloc(sizeof(thread_args_t) * nthreads);

	sems = malloc(sizeof(sem_t) * nthreads);
	for (int i = 0; i < nthreads; i++)
	{
		sem_init(&sems[i], 0, (i == 0 ? 1 : 0));
	}

	xstep = (xmax - xmin) / x_chars;
	ystep = (ymax - ymin) / y_chars;

	for (int i = 0; i < nthreads; i++)
	{
		args[i].thread_id = i;
		args[i].nthreads = nthreads;
		pthread_create(&threads[i], NULL, mandel_thread, &args[i]);
	}

	/*
	 * draw the Mandelbrot Set, one line at a time.
	 * Output is sent to file descriptor '1', i.e., standard output.
	 */

	// for (line = 0; line < y_chars; line++) {
	//       compute_and_output_mandel_line(1, line);
	// }

	for (int i = 0; i < nthreads; i++)
	{
		pthread_join(threads[i], NULL);
	}

	for (int i = 0; i < nthreads; i++)
	{
		sem_destroy(&sems[i]);
	}

	reset_xterm_color(1);
	free(threads);
	free(args);
	free(sems);
	return 0;
}