/* A simple program in C counting the occurence of a character in a file
 * and writing the result in another file
 *
 * Input is given from the command line without further tests:
 * argv[1]: file to read from
 * argv[2]: file to write to
 * argv[3]: character to search for
 *
 * Operating Systems course, CSLab, ECE, NTUA
 *
 */

#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>

int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        printf("Check parameters!\n");
        return 1;
    }

    int file_descriptor_read;
    int file_descriptor_write;
    const char *read_path = argv[1];
    const char *write_path = argv[2];
    char cc, c2c = 'a';
    pid_t child_id;
    pid_t original_code_id;

    /* character to search for (third parameter in command line) */
    c2c = argv[3][0];

    int count = 0;

    /* open file for reading */
    if ((file_descriptor_read = open(read_path, O_RDONLY)) == -1)
    {
        perror("open failed");
        return 1;
    }

    /* open file for writing the result */
    if ((file_descriptor_write = open(write_path, O_WRONLY | O_CREAT | O_TRUNC)) == -1)
    {
        printf("Problem opening file to write\n");
        return 1;
    }

    // Two processes are created
    if ((child_id = fork()) < 0)
    {
        perror("Fork failed");
    }
    else if (child_id == 0)
    {
        printf("Hello world, this is the child process:\n"
               "\t my pid=%d\n"
               "\t parent pid=%d\n",
               getpid(), getppid());

        /* count the occurences of the given character */
        while (read(file_descriptor_read, &cc, 1) != 0)
            if (cc == c2c)
                count++;

        exit(EXIT_SUCCESS);
    }
    else
    {
        // Second child process is created
        if ((original_code_id = fork()) < 0)
        {
            perror("Fork failed");
        }
        // still in the parent process
        else if (original_code_id == 0)
        {
            execv("./original_code.c", argv);
        }
        else
        {
            wait(NULL);

            printf("This is the parent process:\n"
                   "\t my pid=%d\n"
                   "\t child pid=%d\n",
                   getpid(), original_code_id);
        }

        /* close the file for reading */
        close(file_descriptor_read);

        char output[1024];
        snprintf(output, sizeof(output), "The character '%c' appears %d times in file %s.\n", c2c, count, argv[1]);

        write(file_descriptor_write, output, strlen(output));
        /* close the output file */
        close(file_descriptor_write);

        exit(EXIT_SUCCESS);
        return 0;
    }