#include "filetools.h"

int load_file_to_memory(const char *filename, char **buffer)
{
    int size = 0;
    FILE *f = fopen(filename, "rb");
    if (f == NULL)
    {
        *buffer = NULL;

        // -1 means file opening failed
        return -1;
    }

    // figure out the file size
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);

    // allocate buffer, add one more byte for zero termination
    *buffer = (char *)malloc(size + 1);
    if (size != fread(*buffer, sizeof(char), size, f))
    {
        // free the memory in case of an error
        free(*buffer);

        // -2 means file reading failed
        return -2;
    }

    // close the file
    fclose(f);

    // zero terminate
    (*buffer)[size] = 0;

    return size;
}