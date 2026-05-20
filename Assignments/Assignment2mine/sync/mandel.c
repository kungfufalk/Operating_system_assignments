/*
 * mandel.c
 *
 * A program to draw the Mandelbrot Set on a 256-color xterm.
 *
 */

#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>

#include "mandel-lib.h"
#include <semaphore.h>

#define MANDEL_MAX_ITERATION 100000

#define perror_pthread(ret, msg) \
	do                           \
	{                            \
		errno = ret;             \
		perror(msg);             \
	} while (0)

/***************************
 * Compile-time parameters *
 ***************************/

sem_t *sems;

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

void *calculate_lines(void *arg)
{
	thread_args_t *my_args = (thread_args_t *)arg;
	int i = my_args->thread_id;
	int step = my_args->nthreads;
	int color_val[x_chars];

	// compute_mandel_line(i, color_val);
	for (int line = i; line < y_chars; line += step)
	{
		compute_mandel_line(line, color_val);
		sem_wait(&sems[i]);
		output_mandel_line(1, color_val);
		sem_post(&sems[(i + 1) % step]);
	}

	return NULL;
}

int main(int argc, char *argv[])
{

	int line;
	int nthreads = atoi(argv[1]);
	int ret;
	int val;
	pthread_t *threads = malloc(sizeof(pthread_t) * nthreads);
	thread_args_t *args = malloc(sizeof(thread_args_t) * nthreads);

	sems = malloc(sizeof(sem_t) * nthreads);

	if (argc < 2)
	{
		fprintf(stderr, "Usage: %s <nthreads>\n", argv[0]);
		exit(1);
	}

	for (int i = 0; i < nthreads; i++)
	{
		sem_init(&sems[i], 0, (i == 0 ? 1 : 0));
	}

	int thread_line_num = y_chars / nthreads;
	int leftover_rows = y_chars % nthreads;

	xstep = (xmax - xmin) / x_chars;
	ystep = (ymax - ymin) / y_chars;

	for (int i = 0; i < nthreads; i++)
	{
		args[i].thread_id = i;
		args[i].nthreads = nthreads;
		ret = pthread_create(&threads[i], NULL, calculate_lines, &args[i]);
		if (ret)
		{
			perror_pthread(ret, "pthread_create");
			exit(1);
		}
	}

	/*
	 * draw the Mandelbrot Set, one line at a time.
	 * Output is sent to file descriptor '1', i.e., standard output.
	 */

	// for (line = 0; line < y_chars; line++)
	// {
	// 	output_mandel_line(1, line);
	// }

	for (int i = 0; i < nthreads; i++)
	{
		ret = pthread_join(threads[i], NULL);
		if (ret)
		{
			perror_pthread(ret, "pthread_join");
			exit(1);
		}
	}

	reset_xterm_color(1);
	return 0;
}
