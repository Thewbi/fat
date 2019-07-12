#ifndef FILETOOLS_H
#define FILETOOLS_H

#include <stdio.h>
#include <stdlib.h>

/**
 * Opens a file, allocates a buffer the size of the file and reads the file into the buffer
 * Caller is responsible for deallocating the buffer.
 * 
 * return - error codes are negative integers
 *          -1 - file opening error
 *          -2 - file reading error
 *          positive values denote success and are the size of the file in bytes
 */
int load_file_to_memory(const char *filename, char **buffer);

#endif