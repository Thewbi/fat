#include "main.h"

int main()
{
    printf("Starting the application\n");

    char *buffer = NULL;

    // load the file
    if (!load_file_to_memory("/home/wbi/temp/fat.fs", &buffer))
    {
        printf("Loading the file failed!\n");
        return -1;
    }

    //uint16_t value = ((uint16_t)(buffer[12]) << 8) | (uint16_t)buffer[11];

    bios_parameter_block * bpb = (bios_parameter_block *) buffer;

    /*
    // convert endianess
    bpb->bytesPerSec = __bswap_16(bpb->bytesPerSec);
    bpb->rsvdSecCnt = __bswap_16(bpb->rsvdSecCnt);
    bpb->rootEntCnt = __bswap_16(bpb->rootEntCnt);
    bpb->totSec16 = __bswap_16(bpb->totSec16);
    bpb->fatSz16 = __bswap_16(bpb->fatSz16);
    bpb->secPerTrk = __bswap_16(bpb->secPerTrk);
    bpb->numHeads = __bswap_16(bpb->numHeads);
    bpb->hiddSec = __bswap_32(bpb->hiddSec);
    bpb->totSec32 = __bswap_32(bpb->totSec32);
     */

    int fatStartSector = bpb->rsvdSecCnt;
    int fatSectors = bpb->fatSz16 * bpb->numFats;

    int rootDirStartSector = fatStartSector + fatSectors;
    int rootDirSectors = (32 * bpb->rootEntCnt + bpb->bytesPerSec - 1) / bpb->bytesPerSec;

    int dataStartSector = rootDirStartSector + rootDirSectors;


    // CountofClusters from http://elm-chan.org/docs/fat_e.html
    // When the value of bpb->totSec32 on the FAT12/16 volume is less than 0x10000, 
    // this field must be invalid value 0 and the true value is set to BPB_TotSec16. 
    // On the FAT32 volume, this field is always valid and old field is not used.
    //
    
    int dataSectors = -1;
    if (bpb->totSec32 < 0x10000)
    {
        dataSectors = bpb->totSec16 - dataStartSector;
    } 
    else 
    {
        dataSectors = bpb->totSec32 - dataStartSector;
    }

    int countOfClusters = dataSectors / bpb->secPerClus;

    // FAT sub-type (FAT12, FAT16, FAT32) from http://elm-chan.org/docs/fat_e.html
    if (countOfClusters <= 4085)
    {
        printf("FAT12\n");
    } 
    else if (countOfClusters <= 65525)
    {
        printf("FAT16\n");
    } 
    else
    {
        printf("FAT32\n");
    } 

    // clean up
    if (buffer != NULL)
    {
        free(buffer);
        buffer = NULL;
    }

    printf("Terminating the application\n");

    return 0;
}
