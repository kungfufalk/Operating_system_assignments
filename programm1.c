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
#define NUM_PROCESSES 5

int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        printf("Check parameters!\n");
        return 1;
    }

    // File variables
    int final_result = 0;
    int file_descriptor_write;
    const char *read_path = argv[1];
    const char *write_path = argv[2];

    // searched and read character
    char cc, c2c = 'a';

    // process variables
    pid_t original_code_id;
    int pipes_fd[NUM_PROCESSES][2]; // Array of file descriptors for pipes
    pid_t pid[NUM_PROCESSES];       // Array of process IDs

    /* character to search for (third parameter in command line) */
    c2c = argv[3][0];

    int count[NUM_PROCESSES];

    /* open file for reading */
    // if ((file_descriptor_read = open(read_path, O_RDONLY)) == -1)
    // {
    //     perror("open failed");
    //     return 1;
    // }

    /* open file for writing the result */
    if ((file_descriptor_write = open(write_path, O_WRONLY | O_CREAT | O_TRUNC)) == -1)
    {
        printf("Problem opening file to write\n");
        return 1;
    }

    // Create the pipes
    for (int i = 0; i < NUM_PROCESSES; i++)
    {
        if (pipe(pipes_fd[i]) == -1)
        {
            perror("pipe");
            exit(EXIT_FAILURE);
        }
    }

    // exec process is created
    if ((original_code_id = fork()) < 0)
    {
        perror("Fork failed");
        exit(EXIT_FAILURE);
    }
    else if (original_code_id == 0)
    {
        execv("./original_code.c", argv);
    }
    else
    {

        // processes are created
        for (int i = 0; i < NUM_PROCESSES; i++)
        {
            if ((pid[i] = fork()) < 0)
            {
                perror("Fork failed");
            }
            else if (pid[i] == 0)
            {

                count[i] = 0;
                // Each child opens the file independently
                int fd = open(read_path, O_RDONLY);
                if (fd == -1)
                {
                    perror("open in child failed");
                    exit(EXIT_FAILURE);
                }

                // Divide file into NUM_PROCESSES chunks
                struct stat st;
                stat(read_path, &st);                     // get file status
                off_t chunk = st.st_size / NUM_PROCESSES; // chunk size
                lseek(fd, i * chunk, SEEK_SET);           // set offset to corresponding chunk

                off_t bytes_to_read = (i == NUM_PROCESSES - 1)
                                          ? st.st_size - i * chunk // last process reads remainder
                                          : chunk;

                off_t bytes_read = 0;
                while (bytes_read < bytes_to_read && read(fd, &cc, 1) == 1)
                {
                    if (cc == c2c)
                        count[i]++;
                    bytes_read++;
                }

                printf("Hello world, this is the child process:\n"
                       "\t my pid=%d\n"
                       "\t parent pid=%d\n",
                       getpid(), getppid());

                close(fd); // close file

                close(pipes_fd[i][0]);                         // close read end
                write(pipes_fd[i][1], &count[i], sizeof(int)); // write result to pipe
                close(pipes_fd[i][1]);                         // close write end
                exit(EXIT_SUCCESS);
            }
            // else
            // {
            //     printf("This is the parent process:\n"
            //            "\t my pid=%d\n"
            //            "\t child pid=%d\n",
            //            getpid(), original_code_id);
            // }
        }

        // close all write ends of parent
        for (int i = 0; i < NUM_PROCESSES; i++)
            close(pipes_fd[i][1]);

        // Parent process
        for (int i = 0; i < NUM_PROCESSES; i++)
        {
            int status;
            int child_result;

            waitpid(pid[i], &status, 0);

            if (WIFEXITED(status))
            {
                close(pipes_fd[i][1]); // Close the write end of the pipe

                // Read the result from the pipe
                read(pipes_fd[i][0], &child_result, sizeof(child_result));

                printf("Child process %d returned: %d\n", i, child_result);

                close(pipes_fd[i][0]); // Close the read end of the pipe
            }
            final_result = final_result + child_result;
        }

        printf("The character %c occurs in total %d times\n", c2c, final_result);
        // /* close the file for reading */
        // close(file_descriptor_read);

        // char output[1024];
        // snprintf(output, sizeof(output), "The character '%c' appears %d times in file %s.\n", c2c, count, argv[1]);

        // write(file_descriptor_write, output, strlen(output));
        /* close the output file */
        // close(file_descriptor_write);

        exit(EXIT_SUCCESS);
        return 0;
    }
}