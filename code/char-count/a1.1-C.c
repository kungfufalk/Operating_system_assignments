/*
 * a1.1-C.c
 *
 * A simple program in C counting the occurence of a character in a file
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

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {

    FILE *fpr, *fpw;
    char cc, c2c = 'a';
    int count = 0;
    
    /* open file for reading */
    if ((fpr = fopen(argv[1], "r")) == NULL) {
        printf("Problem opening file to read\n");
        return -1; 
    }
    
    /* open file for writing the result */
    if ((fpw = fopen(argv[2], "w+")) == NULL) {
        printf("Problem opening file to write\n");
        return -1; 
    }
    
    /* character to search for (third parameter in command line) */
    c2c = argv[3][0];

    /* count the occurences of the given character */
    while ((cc = fgetc(fpr)) != EOF) if (cc == c2c) count++;
    
    /* close the file for reading */
    fclose(fpr);
    
    /* write the result in the output file */
    fprintf(fpw, "The character '%c' appears %d times in file %s.\n", c2c, count, argv[1]);
    
    /* close the output file */
    fclose(fpw);

    return 0; 
}
