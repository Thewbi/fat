#include "filetools.h"

int load_file_to_memory(const char *filename, char **buffer)
{
    int size = 0;
    FILE *f = fopen(filename, "rb");
    if (f == NULL)
    {
        *buffer = NULL;

        // -1 means file opening fail
        return -1;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    *buffer = (char *)malloc(size + 1);
    if (size != fread(*buffer, sizeof(char), size, f))
    {
        // free the memory in case of an error
        free(*buffer);

        // -2 means file reading fail
        return -2;
    }
    fclose(f);
    (*buffer)[size] = 0;

    return size;
}