#include "main.h"
#include <stdbool.h>

directory_entry *workingDirectory = NULL;

// output date and timestamps of files
// implement cd, pwd, ls
// implement output, create, append, delete of files
// implement create, delete of folders

void rm(const char *buffer, const bios_parameter_block *bpb, const char *filename);
void rmdir(const char *buffer, const bios_parameter_block *bpb, const char *filename);

void fillFat12Entry(const char *buffer, const int fatOffset, const int logicalClusterIndex, fat_12_entry *fat12Entry)
{
    int fatIndex1 = -1;
    if (logicalClusterIndex % 2)
    {
        // odd
        fatIndex1 = (3 * logicalClusterIndex) / 2 + fatOffset;
        fatIndex1 = fatIndex1 - 1;
    }
    else
    {
        // even
        fatIndex1 = (3 * logicalClusterIndex) / 2 + fatOffset;
    }

    // assemble and parse the three byte fat entry
    unsigned char a = buffer[fatIndex1];
    unsigned char b = buffer[fatIndex1 + 1];
    unsigned char c = buffer[fatIndex1 + 2];

    uint32_t fatTouple = (c << 16) + (b << 8) + a;

    // if both values are zero, early out
    if (fatTouple == 0)
    {
        fat12Entry->firstEntry = fat12Entry->secondEntry = 0;
        return;
    }

    // bitmask the first and the second entry
    uint32_t secondLogicalSector = fatTouple & 0xFFF000;
    secondLogicalSector >>= 12;
    uint32_t firstLogicalSector = fatTouple & 0x000FFF;

    fat12Entry->firstEntry = firstLogicalSector;
    fat12Entry->secondEntry = secondLogicalSector;
}

/**
 * Returns the value stored in the FAT in the entry at the logicalClusterIndex
 * 
 * Every three byte block contains two logical fat entries.
 * This method will either return the first or the second fat entry, depending
 * on the logicalClusterIndex passed in.
 */
int readFAT12Entry(const char *buffer, const int fatOffset, const int logicalClusterIndex)
{
    fat_12_entry fat12Entry;
    fillFat12Entry(buffer, fatOffset, logicalClusterIndex, &fat12Entry);

    // return the first or the second entry
    if (logicalClusterIndex % 2)
    {
        return fat12Entry.secondEntry;
    }
    else
    {
        return fat12Entry.firstEntry;
    }
}

void writeFAT12Entry(char *buffer, const int fatOffset, const int logicalClusterIndex, const int newValue)
{
    fat_12_entry fat12Entry;

    // read the current values
    fillFat12Entry(buffer, fatOffset, logicalClusterIndex, &fat12Entry);

    // update the values
    if (logicalClusterIndex % 2)
    {
        fat12Entry.secondEntry = newValue;
    }
    else
    {
        fat12Entry.firstEntry = newValue;
    }

    // assemble
    uint32_t fatTouple = (fat12Entry.secondEntry << 12) | fat12Entry.firstEntry;

    int fatIndex = -1;
    if (logicalClusterIndex % 2)
    {
        // odd
        fatIndex = (3 * logicalClusterIndex) / 2 + fatOffset;
        fatIndex = fatIndex - 1;
    }
    else
    {
        // even
        fatIndex = (3 * logicalClusterIndex) / 2 + fatOffset;
    }

    // assemble and parse the three byte fat entry
    buffer[fatIndex + 2] = (char)((fatTouple & 0xFF0000) >> 16);
    buffer[fatIndex + 1] = (char)((fatTouple & 0xFF00) >> 8);
    buffer[fatIndex + 0] = (char)(fatTouple & 0xFF);
}

bool isDirectory(directory_entry *dirEntry)
{
    return dirEntry->attributes & 0x10;
}

bool isNotDirectory(directory_entry *dirEntry)
{
    return !isDirectory(dirEntry);
}

bool isVolumeLabel(directory_entry *dirEntry)
{
    return dirEntry->attributes & 0x08;
}

bool isNotVolumeLabel(directory_entry *dirEntry)
{
    return !isVolumeLabel(dirEntry);
}

bool isFile(directory_entry *dirEntry)
{
    return !isVolumeLabel(dirEntry) && !isDirectory(dirEntry);
}

bool isNotFile(directory_entry *dirEntry)
{
    return !isFile(dirEntry);
}

// Computes the offset from the beginning of the volume to the data area in sectors.
//
// The organization of a FAT12 system consists of four blocks.
// Their position (sector at which they start) is predefined. (See https://www.google.com/url?sa=t&rct=j&q=&esrc=s&source=web&cd=4&ved=2ahUKEwjolfKslrLjAhVCcZoKHcUMDPwQFjADegQIBRAC&url=http%3A%2F%2Fwww.disc.ua.es%2F~gil%2FFAT12Description.pdf&usg=AOvVaw0vfkjD-j5QnMsNIWTxKuzR)
//
// The four blocks and their start sectors are
// 1. Boot Sector (StartSector: 0)
// 2. Two FAT Tables (StartSector Table 1: 1, StartSector Table 2: 10)
// 3. The root directory (StartSector: 19)
// 4. Data Area (StartSector: 33)
//
// Q: Why subtract 2?
// A: The cluster numbers are linear and are relative to the data area, with cluster 2 being the first cluster of the data area
// the first two clusters are reserved
int16_t dataAreaOffsetInSectors(bios_parameter_block *bpb)
{
    // compute the amount of sectors the root dir occupies
    int rootDirSectors = (32 * bpb->rootEntCnt + bpb->bytesPerSec - 1) / bpb->bytesPerSec;

    // the fat structure is:
    return bpb->rsvdSecCnt + (bpb->secPerFat * bpb->numFats) + rootDirSectors - 2;
}

// returns the offset from the beginning of the file to the first fat in bytes
int16_t fatOffset(bios_parameter_block *bpb, const int fatCopyIndex)
{
    // get pointer to beginning of first fat
    // the first fat is posistioned after all reserved sectors
    int fatOffsetInSectors = bpb->rsvdSecCnt + fatCopyIndex * bpb->secPerFat;
    fatOffsetInSectors *= bpb->bytesPerSec;

    return fatOffsetInSectors;
}

// Converts a logical sector index into the index of the corresponding physical sector.
// The physical sectors are counted from the beginning of the volume
//
// physical sector number = 33 + FAT entry number - 2
// a physical sector is just a sector on the volume
// the first physical sector is the boot sector
// the following sectors are part of reserved sectors or maybe the FAT tables
// Followed by sectors for the root directory
// Followed by sectors for the data area.
//
// Converting the logical sector of a directory entry into a physical sector
// will give you a sector in the data area that contains that file or directory
int16_t logicalToPhysical(bios_parameter_block *bpb, int16_t logicalCluster)
{
    // first two cluster 0 and 1 are reserved, negative logicalClusters do not exist
    if (logicalCluster < 2)
    {
        return -1;
    }

    return dataAreaOffsetInSectors(bpb) + logicalCluster;
}

// outputs all fat entries for debugging purposes
void outputFat(const char *buffer, bios_parameter_block *bpb)
{
    // one fat is sectory per fat multiplied by bytes per sector
    int fatSizeInBytes = bpb->secPerFat * bpb->bytesPerSec;

    // offset to fat
    uint16_t firstFatOffset = fatOffset(bpb, 0);

    // every three bytes in the fat contain two entries
    for (int i = 0; i < (fatSizeInBytes / 3 * 2); i++)
    {
        int value = readFAT12Entry(buffer, firstFatOffset, i);
        printf("entry: %d value: %d\n", i, value);
    }

    printf("\n");
}

// outputs a file to the console by following all sectors in the chain of sectors
void outputFile(const char *buffer, bios_parameter_block *bpb, const int firstLogicalClusterIndex)
{
    int16_t dataAreaOffsetInBytes = dataAreaOffsetInSectors(bpb) * bpb->bytesPerSec;
    uint16_t firstFatOffset = fatOffset(bpb, 0);

    int logicalClusterIndex = firstLogicalClusterIndex;

    //while (logicalClusterIndex > 1 && logicalClusterIndex != FAT12_LAST_CLUSTER_IN_CHAIN && logicalClusterIndex != 0xFF0 && logicalClusterIndex != FAT12_DEFECTIVE_CLUSTER)
    while (logicalClusterIndex > 1 && logicalClusterIndex != FAT12_LAST_CLUSTER_IN_CHAIN && logicalClusterIndex != FAT12_DEFECTIVE_CLUSTER)
    {
        // print the physical sector
        char *bufferPtr = buffer;
        bufferPtr += (dataAreaOffsetInBytes + logicalClusterIndex * bpb->bytesPerSec);
        printf("%.512s", bufferPtr);

        // read next sector in the chain of sectors from the fat
        logicalClusterIndex = readFAT12Entry(buffer, firstFatOffset, logicalClusterIndex);
    }

    if (logicalClusterIndex == FAT12_DEFECTIVE_CLUSTER)
    {
        printf("Defective cluster detected!\n");
    }

    printf("\n");
}

int findLastCluster(const char *buffer, bios_parameter_block *bpb, directory_entry *entry)
{
    // security check
    if (entry->first_logical_cluster == 0)
    {
        return -1;
    }

    uint16_t firstFatOffset = fatOffset(bpb, 0);

    int logicalClusterIndex = entry->first_logical_cluster;
    int nextLogicalClusterIndex = entry->first_logical_cluster;
    //while (logicalClusterIndex > 1 && logicalClusterIndex != FAT12_LAST_CLUSTER_IN_CHAIN && logicalClusterIndex != 0xFF0 && logicalClusterIndex != FAT12_DEFECTIVE_CLUSTER)
    while (nextLogicalClusterIndex > 1 && nextLogicalClusterIndex != FAT12_LAST_CLUSTER_IN_CHAIN && nextLogicalClusterIndex != FAT12_DEFECTIVE_CLUSTER)
    {
        // read next sector in the chain of sectors from the fat
        logicalClusterIndex = nextLogicalClusterIndex;
        nextLogicalClusterIndex = readFAT12Entry(buffer, firstFatOffset, nextLogicalClusterIndex);
    }

    if (logicalClusterIndex == FAT12_DEFECTIVE_CLUSTER)
    {
        printf("Defective cluster detected!\n");
        return -1;
    }

    return logicalClusterIndex;
}

void outputDirectoryEntry(directory_entry *dirEntry)
{
    // http: //alexander.khleuven.be/courses/bs1/fat12/fat12.html
    printf("filename: %.11s ReadOnly: %s, Hidden: %s, SystemFile: %s, VolumeLabel: %s, Directory: %s, ShouldBeArchived: %s, FirstLogicalCluster: %d \n",
           dirEntry->filename,
           (dirEntry->attributes & 0x01 ? "true" : "false"), // readonly
           (dirEntry->attributes & 0x02 ? "true" : "false"), // hidden
           (dirEntry->attributes & 0x04 ? "true" : "false"), // system file
           (dirEntry->attributes & 0x08 ? "true" : "false"), // is volumeLabel
           (dirEntry->attributes & 0x10 ? "true" : "false"), // is directory
           (dirEntry->attributes & 0x20 ? "true" : "false"), // should be archived
           dirEntry->first_logical_cluster);

    // TODO: output dates and timestamps
}

bool isLink(const char *foldername)
{
    // cannot delete the parent folder
    if (strlen(foldername) == 2 && strcmp(foldername, "..") == 0)
    {
        return true;
    }

    // cannot delete the current folder
    if (strlen(foldername) == 1 && strcmp(foldername, ".") == 0)
    {
        return true;
    }

    return false;
}

/**
 * Iterate directory entries for output to the console.
 */
int iterateEntries(directory_entry *directoryEntryPtr, const int entryCount, bool returnLinks)
{
    int entriesUsed = 0;

    // output all entries
    for (int i = 0; i < entryCount; i++)
    {
        // If the first byte of the Filename field is 0xE5, then the directory entry is free
        // (i.e., currently unused), and hence there is no file or subdirectory associated with the directory entry.
        //
        // If the first byte of the Filename field is 0x00, then this directory entry is free and all
        // the remaining directory entries in this directory are also free.
        if (directoryEntryPtr->filename[0] == DIRECTORY_ENTRY_FREE)
        {
            // go to the next entry
            directoryEntryPtr++;
            continue;
        }
        else if (directoryEntryPtr->filename[0] == DIRECTORY_ENTRY_LAST)
        {
            // break the loop, because all subsequent entries are free too
            break;
        }

        outputDirectoryEntry(directoryEntryPtr);

        // count all entries that contain real folders or files
        // do not count the . and .. (links)
        if (returnLinks || !isLink(directoryEntryPtr->filename))
        {
            entriesUsed++;
        }

        // go to next entry
        directoryEntryPtr++;
    }

    return entriesUsed;
}

/**
 * Given a pointer to directory entries (root directory or directory in the data area alike) and the 
 * number of directory entries, returns the entry with the given filename
 */
directory_entry *findDirectoryEntry(directory_entry *directoryEntryPtr, const int entryCount, const char *filename)
{
    // output all entries
    for (int i = 0; i < entryCount; i++)
    {
        // If the first byte of the Filename field is 0xE5, then the directory entry is free
        // (i.e., currently unused), and hence there is no file or subdirectory associated with the directory entry.
        //
        // If the first byte of the Filename field is 0x00, then this directory entry is free and all
        // the remaining directory entries in this directory are also free.
        if (directoryEntryPtr->filename[0] == DIRECTORY_ENTRY_FREE)
        {
            // go to the next entry
            directoryEntryPtr++;
            continue;
        }
        else if (directoryEntryPtr->filename[0] == DIRECTORY_ENTRY_LAST)
        {
            // break the loop, because all subsequent entries are free too
            break;
        }

        // TODO: this matching is prefix matching and hence incorrect
        // a absolute match is needed
        if (strncmp(directoryEntryPtr->filename, filename, FILENAME_LENGTH) == 0)
        {
            return directoryEntryPtr;
        }

        directoryEntryPtr++;
    }

    return NULL;
}

/**
 * In a directory_entry, if the value of the first Logical Cluster is “0”, then it refers to the first cluster of the root 
 * directory and that directory entry is therefore describing the root directory. 
 * (Keep in mind that the root directory is listed as the “..” entry i.e. the parent directory in all its sub-directories.)
 * 
 * In a directory_entry, if the first byte of the Filename field is 0xE5, then the directory entry is free 
 * (i.e., currently unused), and hence there is no file or subdirectory associated with the directory entry.
 * 
 * If the first byte of the Filename field is 0x00, then this directory entry is free and all the remaining 
 * directory entries in this directory are also free.          
 */
int outputFolder(const char *buffer, bios_parameter_block *bpb, const int firstLogicalClusterIndex)
{
    int entriesUsed = 0;
    int16_t dataAreaOffsetInBytes = dataAreaOffsetInSectors(bpb) * bpb->bytesPerSec;
    uint16_t firstFatOffset = fatOffset(bpb, 0);

    int logicalClusterIndex = firstLogicalClusterIndex;

    //while (logicalClusterIndex > 1 && logicalClusterIndex != FAT12_LAST_CLUSTER_IN_CHAIN && logicalClusterIndex != 0xFF0 && logicalClusterIndex != FAT12_DEFECTIVE_CLUSTER)
    while (logicalClusterIndex > 1 && logicalClusterIndex != FAT12_LAST_CLUSTER_IN_CHAIN && logicalClusterIndex != FAT12_DEFECTIVE_CLUSTER)
    {
        // pointer to physical sector
        char *bufferPtr = buffer;
        bufferPtr += (dataAreaOffsetInBytes + logicalClusterIndex * bpb->bytesPerSec);

        // cast to directory entry
        directory_entry *directoryEntryPtr = (directory_entry *)bufferPtr;

        // output all entries
        bool returnLinks = false;
        entriesUsed += iterateEntries(directoryEntryPtr, DIR_ENTRIES_PER_SECTOR, returnLinks);

        // read next sector in the chain of sectors from the fat
        logicalClusterIndex = readFAT12Entry(buffer, firstFatOffset, logicalClusterIndex);
    }

    if (logicalClusterIndex == FAT12_DEFECTIVE_CLUSTER)
    {
        printf("Defective cluster detected!\n");
    }

    printf("\n");

    return entriesUsed;
}

/**
 * Compute the offset in bytes and return a pointer to the root directory.
 * The root directory is stored after the reserved sectors and the redundant FATs. 
 */
directory_entry *findRootDirectoryEntries(const char *buffer, bios_parameter_block *bpb)
{
    // compute the sector where the root directory starts
    // reserved Sector count tells us how many sectors are reserved for boot information
    // secPerFat contains the sectors used for each FAT table
    // numFats is the amount of copies of the FAT. For crash-safetry, the FAT is duplicated to have it redundand
    // Copies of the FAT are still available even if one of the copies is corrupted.
    int rootDirectoryOffsetInSectors = bpb->rsvdSecCnt + bpb->secPerFat * bpb->numFats;

    // convert the offset in sectors to an offset in bytes
    int rootDirectoryOffsetInBytes = rootDirectoryOffsetInSectors * bpb->bytesPerSec;

    char *ptr = buffer;
    ptr += rootDirectoryOffsetInBytes;

    return (directory_entry *)ptr;
}

/**
 * Outputs the third section of the FAT volume which is the root directory.
 */
int outputRootFolder(const char *buffer, bios_parameter_block *bpb)
{
    bool returnLinks = false;
    int entriesUsed = iterateEntries(findRootDirectoryEntries(buffer, bpb), bpb->rootEntCnt, returnLinks);

    printf("\n");

    return entriesUsed;
}

int ls(const char *buffer, const bios_parameter_block *bpb)
{
    return lsDirEntry(buffer, bpb, workingDirectory);
}

int lsDirEntry(const char *buffer, const bios_parameter_block *bpb, directory_entry *directoryEntry)
{
    if (directoryEntry == NULL)
    {
        return outputRootFolder(buffer, bpb);
    }

    return outputFolder(buffer, bpb, directoryEntry->first_logical_cluster);
}

/**
 * Find an entry in a directory that is stored in the data area
 */
directory_entry *findEntryInFolder(const char *buffer, bios_parameter_block *bpb, const int firstLogicalClusterIndex, const char *filename)
{
    int16_t dataAreaOffsetInBytes = dataAreaOffsetInSectors(bpb) * bpb->bytesPerSec;
    uint16_t firstFatOffset = fatOffset(bpb, 0);

    int logicalClusterIndex = firstLogicalClusterIndex;

    //while (logicalClusterIndex > 1 && logicalClusterIndex != FAT12_LAST_CLUSTER_IN_CHAIN && logicalClusterIndex != 0xFF0 && logicalClusterIndex != FAT12_DEFECTIVE_CLUSTER)
    while (logicalClusterIndex > 1 && logicalClusterIndex != FAT12_LAST_CLUSTER_IN_CHAIN && logicalClusterIndex != FAT12_DEFECTIVE_CLUSTER)
    {
        // pointer to physical sector
        char *bufferPtr = buffer;
        bufferPtr += (dataAreaOffsetInBytes + logicalClusterIndex * bpb->bytesPerSec);

        // convert the filename
        char convertedFilename[FILENAME_LENGTH];
        memset(convertedFilename, 0, FILENAME_LENGTH);
        filenameToFatElevenThree(filename, convertedFilename, FILENAME_LENGTH);

        // cast to directory entry
        directory_entry *directoryEntryPtr = (directory_entry *)bufferPtr;
        directory_entry *entry = findDirectoryEntry(directoryEntryPtr, DIR_ENTRIES_PER_SECTOR, convertedFilename);
        if (entry != NULL)
        {
            return entry;
        }

        // read next sector in the chain of sectors from the fat
        logicalClusterIndex = readFAT12Entry(buffer, firstFatOffset, logicalClusterIndex);
    }

    if (logicalClusterIndex == FAT12_DEFECTIVE_CLUSTER)
    {
        printf("Defective cluster detected!\n");
    }

    return NULL;
}

void cd(const char *buffer, const bios_parameter_block *bpb, const char *foldername)
{
    directory_entry *entry = NULL;
    if (workingDirectory == NULL)
    {
        // convert the filename
        char convertedFoldername[FILENAME_LENGTH];
        memset(convertedFoldername, 0, FILENAME_LENGTH);
        filenameToFatElevenThree(foldername, convertedFoldername, FILENAME_LENGTH);

        directory_entry *entries = findRootDirectoryEntries(buffer, bpb);
        entry = findDirectoryEntry(entries, bpb->rootEntCnt, convertedFoldername);
    }
    else
    {
        entry = findEntryInFolder(buffer, bpb, workingDirectory->first_logical_cluster, foldername);
    }

    workingDirectory = NULL;
    if (entry == NULL || isFile(entry))
    {
        // convert the filename (for debug output only)
        char convertedFoldername[FILENAME_LENGTH];
        memset(convertedFoldername, 0, FILENAME_LENGTH);
        filenameToFatElevenThree(foldername, convertedFoldername, FILENAME_LENGTH);

        printf("Cannot find folder '%.11s' (%s). It does not exist or is not a folder!\n", convertedFoldername, foldername);
        return;
    }

    // if the user executed cd .. and .. is the root directory, then leave the workingDirectory variable as NULL
    // if .. points to the root directory, its first logical cluster contains the value 0
    if (entry->first_logical_cluster == 0)
    {
        return;
    }

    workingDirectory = entry;
}

directory_entry *findFile(const char *buffer, const bios_parameter_block *bpb, const char *filename)
{
    directory_entry *entry = NULL;

    if (workingDirectory == NULL)
    {
        // convert the filename
        char convertedFilename[FILENAME_LENGTH];
        memset(convertedFilename, 0, FILENAME_LENGTH);
        filenameToFatElevenThree(filename, convertedFilename, FILENAME_LENGTH);

        directory_entry *entries = findRootDirectoryEntries(buffer, bpb);
        entry = findDirectoryEntry(entries, bpb->rootEntCnt, convertedFilename);
    }
    else
    {
        entry = findEntryInFolder(buffer, bpb, workingDirectory->first_logical_cluster, filename);
    }

    return entry;
}

void outputFileByName(const char *buffer, const bios_parameter_block *bpb, const char *filename)
{
    directory_entry *entry = findFile(buffer, bpb, filename);

    if (entry == NULL || isNotFile(entry))
    {
        // convert the filename (for debug output only)
        char convertedFilename[FILENAME_LENGTH];
        memset(convertedFilename, 0, FILENAME_LENGTH);
        filenameToFatElevenThree(filename, convertedFilename, FILENAME_LENGTH);

        printf("Cannot find file '%.11s' (%s). It does not exist or is not a file!\n", convertedFilename, filename);
        return;
    }

    // security check
    if (entry->first_logical_cluster == 0)
    {
        return;
    }

    outputFile(buffer, bpb, entry->first_logical_cluster);
}

/**
 * Checks the cluster/sector that the directory_entry points to, if there is space left, for another directory entry
 * 
 *   - check if the sector pointed to by the direntry is indeed a folder
 *   - check if the direntry has a free entry
 * 
 * A cluster/sector is 512 bytes, a directory entry is 32 bytes, it follows that a cluster/sector
 *     can store up to 16 entries.
 * 
 * returns the free entry or NULL if nothing is free
 */
directory_entry *findFreeDirEntry(const char *buffer, const bios_parameter_block *bpb)
{
    directory_entry *directoryEntryPtr = NULL;
    bool found = false;

    // if the workingDirectory variable is NULL, it means that the user is currently looking at the root directory
    if (workingDirectory == NULL)
    {
        directoryEntryPtr = findRootDirectoryEntries(buffer, bpb);

        for (int i = 0; i < bpb->rootEntCnt; i++)
        {
            // If the first byte of the Filename field is 0xE5, then the directory entry is free
            // (i.e., currently unused)
            //
            // If the first byte of the Filename field is 0x00, then this directory entry is free and all
            // the remaining directory entries in this directory are also free.
            if (directoryEntryPtr->filename[0] == DIRECTORY_ENTRY_FREE || directoryEntryPtr->filename[0] == DIRECTORY_ENTRY_LAST)
            {
                found = true;
                break;
            }

            directoryEntryPtr++;
        }
    }
    else
    {

        int16_t dataAreaOffsetInBytes = dataAreaOffsetInSectors(bpb) * bpb->bytesPerSec;
        uint16_t firstFatOffset = fatOffset(bpb, 0);

        int logicalClusterIndex = workingDirectory->first_logical_cluster;

        //while (logicalClusterIndex > 1 && logicalClusterIndex != FAT12_LAST_CLUSTER_IN_CHAIN && logicalClusterIndex != 0xFF0 && logicalClusterIndex != FAT12_DEFECTIVE_CLUSTER)
        while (logicalClusterIndex > 1 && logicalClusterIndex != FAT12_LAST_CLUSTER_IN_CHAIN && logicalClusterIndex != FAT12_DEFECTIVE_CLUSTER)
        {
            // pointer to physical sector
            char *bufferPtr = buffer;
            bufferPtr += (dataAreaOffsetInBytes + logicalClusterIndex * bpb->bytesPerSec);

            directoryEntryPtr = (directory_entry *)bufferPtr;

            for (int i = 0; i < DIR_ENTRIES_PER_SECTOR; i++)
            {
                // If the first byte of the Filename field is 0xE5, then the directory entry is free
                // (i.e., currently unused)
                //
                // If the first byte of the Filename field is 0x00, then this directory entry is free and all
                // the remaining directory entries in this directory are also free.
                if (directoryEntryPtr->filename[0] == DIRECTORY_ENTRY_FREE || directoryEntryPtr->filename[0] == DIRECTORY_ENTRY_LAST)
                {
                    found = true;
                    break;
                }

                directoryEntryPtr++;
            }

            // read next sector in the chain of sectors from the fat
            logicalClusterIndex = readFAT12Entry(buffer, firstFatOffset, logicalClusterIndex);
        }

        // int16_t logicalClusterIndex = workingDirectory->first_logical_cluster;
        // if (logicalClusterIndex < 2 || logicalClusterIndex == FAT12_DEFECTIVE_CLUSTER || logicalClusterIndex == 0x0)
        // {
        //     return NULL;
        // }

        // // pointer to physical sector (convert logical to physical)
        // int16_t dataAreaOffsetInBytes = dataAreaOffsetInSectors(bpb) * bpb->bytesPerSec;
        // char *bufferPtr = buffer;
        // bufferPtr += (dataAreaOffsetInBytes + logicalClusterIndex * bpb->bytesPerSec);

        // directoryEntryPtr = (directory_entry *)bufferPtr;

        // for (int i = 0; i < DIR_ENTRIES_PER_SECTOR; i++)
        // {
        //     // If the first byte of the Filename field is 0xE5, then the directory entry is free
        //     // (i.e., currently unused)
        //     //
        //     // If the first byte of the Filename field is 0x00, then this directory entry is free and all
        //     // the remaining directory entries in this directory are also free.
        //     if (directoryEntryPtr->filename[0] == DIRECTORY_ENTRY_FREE || directoryEntryPtr->filename[0] == DIRECTORY_ENTRY_LAST)
        //     {
        //         found = true;
        //         break;
        //     }

        //     directoryEntryPtr++;
        // }
    }

    return found ? directoryEntryPtr : NULL;
}

/**
 * Finds a free cluster/sector by looking through the FAT for the first free entry
 * 
 * returns the logical index of the free cluster or -1 if there is no free cluster left
 */
int16_t findFreeLogicalCluster(const char *buffer, const bios_parameter_block *bpb)
{
    // one fat is sectory per fat multiplied by bytes per sector
    int fatSizeInBytes = bpb->secPerFat * bpb->bytesPerSec;

    // offset to fat
    uint16_t firstFatOffset = fatOffset(bpb, 0);

    // every three bytes in the fat contain two entries
    for (int i = 0; i < (fatSizeInBytes / 3 * 2); i++)
    {
        // value of zero means the FAT contains a free entry at this logical index
        int value = readFAT12Entry(buffer, firstFatOffset, i);
        if (value == 0)
        {
            return i;
        }
    }

    return -1;
}

void writeFAT(const char *buffer, const bios_parameter_block *bpb, int16_t chainStart, int16_t newValue)
{
    int16_t dataAreaOffsetInBytes = dataAreaOffsetInSectors(bpb) * bpb->bytesPerSec;
    uint16_t firstFatOffset = fatOffset(bpb, 0);

    int oldLogicalClusterIndex = chainStart;
    int logicalClusterIndex = chainStart;

    //while (logicalClusterIndex > 1 && logicalClusterIndex != FAT12_LAST_CLUSTER_IN_CHAIN && logicalClusterIndex != 0xFF0 && logicalClusterIndex != FAT12_DEFECTIVE_CLUSTER)
    while (logicalClusterIndex > 1 && logicalClusterIndex != FAT12_LAST_CLUSTER_IN_CHAIN && logicalClusterIndex != FAT12_DEFECTIVE_CLUSTER)
    {
        // print the physical sector
        char *bufferPtr = buffer;
        bufferPtr += (dataAreaOffsetInBytes + logicalClusterIndex * bpb->bytesPerSec);

        // read next sector in the chain of sectors from the fat
        oldLogicalClusterIndex = logicalClusterIndex;
        logicalClusterIndex = readFAT12Entry(buffer, firstFatOffset, logicalClusterIndex);
    }

    if (logicalClusterIndex == FAT12_DEFECTIVE_CLUSTER)
    {
        printf("Defective cluster detected!\n");
        return;
    }

    for (int i = 0; i < bpb->numFats; i++)
    {
        writeFAT12Entry(buffer, fatOffset(bpb, i), oldLogicalClusterIndex, newValue);
    }
}

/**
 * Appends a sector to the current sector chain the directory entry points to
 * 
 *   - Checks if there is a free sector left in the data area
 *   - Updates all FATs
 * 
 * entry->first_logical_cluster - the start of the chain to append a cluster/sector to
 */
int16_t appendClusterSectorToChain(const char *buffer, const bios_parameter_block *bpb, directory_entry *entry)
{
    int16_t freeLogicalIndex = findFreeLogicalCluster(buffer, bpb);
    if (freeLogicalIndex == -1)
    {
        return -1;
    }

    writeFAT(buffer, bpb, entry->first_logical_cluster, freeLogicalIndex);
    writeFAT(buffer, bpb, freeLogicalIndex, FAT12_LAST_CLUSTER_IN_CHAIN);
    //outputFat(buffer, bpb);

    return freeLogicalIndex;
}

char *initializeDirectorySector(const char *buffer, const bios_parameter_block *bpb, const bool addLinks, const int16_t logicalCluster, const int16_t parentLogicalCluster)
{
    // write empty directory entries into the sector
    int16_t physicalSector = logicalToPhysical(bpb, logicalCluster);
    char *ptr = buffer + (physicalSector * bpb->bytesPerSec);
    directory_entry *directoryEntryPtr = (directory_entry *)ptr;

    // initialize all entries
    for (int i = 0; i < (bpb->bytesPerSec / sizeof(directory_entry)); i++)
    {
        // clear entry
        memset(directoryEntryPtr, 0, sizeof(directory_entry));

        // configure entry
        directoryEntryPtr->attributes |= DIRECTORY_FLAG;
        directoryEntryPtr->filename[0] = DIRECTORY_ENTRY_FREE;

        // go to the next entry
        directoryEntryPtr++;
    }

    if (addLinks)
    {
        directory_entry *firstEntryPtr = (directory_entry *)ptr;
        firstEntryPtr->filename[0] = '.';
        firstEntryPtr->first_logical_cluster = logicalCluster;

        directory_entry *secondEntryPtr = (directory_entry *)ptr;
        secondEntryPtr++;
        secondEntryPtr->filename[0] = '.';
        secondEntryPtr->filename[1] = '.';
        secondEntryPtr->first_logical_cluster = parentLogicalCluster;
    }

    return ptr;
}

directory_entry *prepareDirectoryEntry(const char *buffer, const bios_parameter_block *bpb)
{
    // find a free directory entry
    directory_entry *directoryEntry = findFreeDirEntry(buffer, bpb);

    // could retrieve entry
    if (directoryEntry != NULL)
    {
        // clear the directory entry
        memset(directoryEntry, 0, sizeof(directory_entry));
        directoryEntry->filename[0] = DIRECTORY_ENTRY_FREE;

        return directoryEntry;
    }

    // if no directory entry is free, try to create one

    // in root directory or not?
    if (workingDirectory == NULL)
    {
        // if the root directory is full, there is no way to add an entry
        // as the root directory is fixed in size and cannot grow as files or folders stored
        // in the data area can
        printf("Cannot create new folder! No free root directory entries are left!\n");
        return NULL;
    }
    else
    {
        // if this is a folder in the data area and not in the root directory, add a sector
        // TEST, create a folder and add more than 16 records to it, record 17 will hit this branch
        int16_t logicalCluster = appendClusterSectorToChain(buffer, bpb, workingDirectory);
        if (logicalCluster == -1)
        {
            printf("Cannot create new folder! No space left!\n");
            return NULL;
        }

        bool addLinks = false;
        char *ptr = initializeDirectorySector(buffer, bpb, addLinks, logicalCluster, workingDirectory == NULL ? 0 : workingDirectory->first_logical_cluster);

        // set the pointer to the first entry
        directoryEntry = (directory_entry *)ptr;
    }

    // clear the directory entry
    memset(directoryEntry, 0, sizeof(directory_entry));
    directoryEntry->filename[0] = DIRECTORY_ENTRY_FREE;

    return directoryEntry;
}

/**
 * Two parts to remember:
 *   - check if the current directory already contains a folder of that name
 *   - if in the root directory, check if there is a free directory entry available, if not fail
 *     The root directory holds exactly bpb->rootEntCnt entries 
 *   - Find a free cluster in the data area to store the directories direntrires data into. 
 *     If non is free, fail, otherwise remember that logical index.
 *   - if NOT in the root directory, check if the current directory needs to be extended by another cluster/sector to store the new
 *     directory index. A cluster/sector is 512 bytes, a directory entry is 32 bytes, it follows that a cluster/sector
 *     can store up to 16 entries. If all are used, you need to append a new cluster/sector to the directory
 *   - create a directory entry in the current directory and save the logical cluster into it, along with the changed folder name
 *     For this operation, remember to update all FATs!
 */
void mkdir(const char *buffer, const bios_parameter_block *bpb, const char *foldername)
{
    directory_entry *directoryEntry = prepareDirectoryEntry(buffer, bpb);
    if (directoryEntry == NULL)
    {
        return;
    }

    // convert the filename
    char convertedFoldername[FILENAME_LENGTH];
    memset(convertedFoldername, 0, FILENAME_LENGTH);
    filenameToFatElevenThree(foldername, convertedFoldername, FILENAME_LENGTH);

    // set the filename
    memcpy(directoryEntry->filename, convertedFoldername, FILENAME_LENGTH);

    // set the flags, make it a directory
    directoryEntry->attributes |= DIRECTORY_FLAG;

    // find a free cluster in the data area, attach it to the directory entry
    int16_t freeSectorLogicalIndex = findFreeLogicalCluster(buffer, bpb);
    if (freeSectorLogicalIndex == -1)
    {
        printf("Cannot create new folder! No free sectors are left!\n");
        return;
    }
    directoryEntry->first_logical_cluster = freeSectorLogicalIndex;

    // write the last sector marker 0xFFF into the FAT to show that the new folder currently only
    // uses this one sector
    writeFAT(buffer, bpb, freeSectorLogicalIndex, 0xFFF);

    // insert directory entries into the sector
    bool addLinks = true;
    initializeDirectorySector(buffer, bpb, addLinks, freeSectorLogicalIndex, workingDirectory == NULL ? 0 : workingDirectory->first_logical_cluster);
}

/**
 * Deletes a folder
 * 
 * 0. Check if the folder exists and is not read only in its directory entries attributes.
 * 
 * If the directory is not empty, it cannot be deleted.
 * 
 * Set the directory entry to free (0xE5 as first byte in the filename)
 * if there are no used directory entries after this one, set the first byte in the filename to 0x00
 * 
 * If the cluster is contained in the data area not in the root directory
 * and the entire cluster consist only of empty directory entries, remove this cluster from the
 * folders cluster chain, if it is the last cluster in the cluster chain!
 * 
 * Delete the entire cluster chain of the folder.
 */
void rmdir(const char *buffer, const bios_parameter_block *bpb, const char *filename)
{
    // cannot delete the parent folder
    if (strlen(filename) == 2 && strcmp(filename, "..") == 0)
    {
        return;
    }

    // cannot delete the current folder
    if (strlen(filename) == 1 && strcmp(filename, ".") == 0)
    {
        return;
    }

    directory_entry *directoryEntry = findFile(buffer, bpb, filename);

    // if the file does not exist, return
    if (directoryEntry == NULL)
    {
        return;
    }

    // if it is not a directory, return
    if ((directoryEntry->attributes & DIRECTORY_FLAG) == 0)
    {
        return;
    }

    // if the directory is not empty, return
    int entriesUsed = lsDirEntry(buffer, bpb, directoryEntry);
    if (entriesUsed > 0)
    {
        printf("Cannot delete the folder because it is not empty!\n");
        return;
    }

    rm(buffer, bpb, filename);
}

/**
 * Creates an empty file in the working directory if no file of the same name exists.
 * If a file exists already, returns 0.
 * In case of errors, returns a negative integer.
 * 
 * 0. Check if a file off the same name exists
 * 
 * 1. Find a free directory entry.
 *   - if none is free in the root directory ==> failure
 *   - if none is free in a data area folder, append a cluster, insert directory_entries without links, return the first.
 *     If no clusters are left ==> failure
 * 
 * 2. Find a free cluster in the fat, mark it as used in all fats, write that logical cluster index into the directory entry's
 * first logical cluster
 * 
 * 3. Write the 8.3 converted filename into the directory entry
 * 
 * 4. Set the file bit into the directory entry's attributes
 * 
 * 5. set the timestamps in the directory entry
 */
int touch(const char *buffer, const bios_parameter_block *bpb, const char *filename, directory_entry **outDirectoryEntry)
{
    if (strlen(filename) == 0)
    {
        printf("Cannot create new file because no filename was specified!\n");
        return -1;
    }

    directory_entry *directoryEntry = findFile(buffer, bpb, filename);
    if (directoryEntry != NULL)
    {
        // convert the filename
        char convertedFilename[FILENAME_LENGTH];
        memset(convertedFilename, 0, FILENAME_LENGTH);
        filenameToFatElevenThree(filename, convertedFilename, FILENAME_LENGTH);

        printf("Cannot create new file %s! A file or folder with the same name exists!\n", convertedFilename);

        // fill the out parameter
        if (outDirectoryEntry != NULL)
        {
            *outDirectoryEntry = directoryEntry;
        }

        // return a success code because the file exists
        return 0;
    }

    // find a free directory entry
    directoryEntry = prepareDirectoryEntry(buffer, bpb);
    if (directoryEntry == NULL)
    {
        printf("Cannot create new file! There is no space for a directory entry left!\n");
        return -3;
    }

    // create and attach a cluster
    int16_t freeSectorLogicalIndex = findFreeLogicalCluster(buffer, bpb);
    if (freeSectorLogicalIndex == -1)
    {
        printf("Cannot create new file! No free sectors are left!\n");
        return -4;
    }
    directoryEntry->first_logical_cluster = freeSectorLogicalIndex;

    // write the last sector marker 0xFFF into the FAT to show that the new file currently only
    // uses this one sector
    writeFAT(buffer, bpb, freeSectorLogicalIndex, FAT12_LAST_CLUSTER_IN_CHAIN);

    // convert the filename
    char convertedName[FILENAME_LENGTH];
    memset(convertedName, 0, FILENAME_LENGTH);
    filenameToFatElevenThree(filename, convertedName, FILENAME_LENGTH);

    // set the filename
    memcpy(directoryEntry->filename, convertedName, FILENAME_LENGTH);

    // fill the out parameter
    if (outDirectoryEntry != NULL)
    {
        *outDirectoryEntry = directoryEntry;
    }

    return 0;
}

/**
 * Appends data to a file
 * 
 * If the file does not exist in the working directory, touch it (call touch())
 * 
 * get the last sector from the cluster chain
 * append to that sector if the data fits
 * 
 * If the data does not fit, call appendClusterSectorToChain() and write the data
 * 
 * update the filesize
 */
int appendToFile(const char *buffer, const bios_parameter_block *bpb, const char *filename, const char *data, const int dataLen)
{
    int bytesWritten = 0;

    if (dataLen <= 0)
    {
        printf("dataLen is negative or zero! Aborting write!\n");
        return bytesWritten;
    }

    directory_entry *directoryEntry = NULL;
    if (touch(buffer, bpb, filename, &directoryEntry) < 0)
    {
        // convert the filename
        char convertedName[FILENAME_LENGTH];
        memset(convertedName, 0, FILENAME_LENGTH);
        filenameToFatElevenThree(filename, convertedName, FILENAME_LENGTH);

        printf("Cannot find or create file %s!\n", convertedName);
        return bytesWritten;
    }

    // find the logical index of the last cluster
    int logicalIndex = findLastCluster(buffer, bpb, directoryEntry);

    int bytesToWrite = dataLen;

    // determine how many bytes are used in that cluster
    int bytesUsed = directoryEntry->filesize % bpb->bytesPerSec;
    int bytesLeft = bpb->bytesPerSec - bytesUsed;

    char *dataPtr = data;

    while (bytesToWrite > 0)
    {
        int bytesToWriteIntoCluster = bytesLeft < bytesToWrite ? bytesLeft : bytesToWrite;

        // get pointer to physical cluster
        // int16_t dataAreaOffsetInBytes = dataAreaOffsetInSectors(bpb) * bpb->bytesPerSec;
        char *ptr = buffer;
        ptr += (logicalToPhysical(bpb, logicalIndex) * bpb->bytesPerSec);

        // move pointer after the data currently stored in the cluster
        ptr += bytesUsed;

        // append bytesToWriteIntoCluster to last cluster
        memcpy(ptr, dataPtr, bytesToWriteIntoCluster);
        bytesWritten += bytesToWriteIntoCluster;

        // move data ptr because we just consumed bytes
        dataPtr += bytesToWriteIntoCluster;

        // update the loop condition
        bytesToWrite -= bytesToWriteIntoCluster;

        bytesUsed += bytesToWriteIntoCluster;
        bytesLeft -= bytesToWriteIntoCluster;

        // if the cluster is used completely, add a new cluster
        if (bytesToWrite > 0 && bytesLeft == 0)
        {
            logicalIndex = appendClusterSectorToChain(buffer, bpb, directoryEntry);
            bytesUsed = 0;
            bytesLeft = bpb->bytesPerSec - bytesUsed;
        }
    }

    // Update filesize in the directory entry
    directoryEntry->filesize += dataLen;

    return bytesWritten;
}

void collapseTheFolder(const char *buffer, const bios_parameter_block *bpb, directory_entry *directoryEntry)
{
    int lastUsedLogicalSector = 0;
    int16_t dataAreaOffsetInBytes = dataAreaOffsetInSectors(bpb) * bpb->bytesPerSec;
    uint16_t firstFatOffset = fatOffset(bpb, 0);

    int logicalClusterIndex = directoryEntry->first_logical_cluster;

    while (logicalClusterIndex > 1 && logicalClusterIndex != FAT12_LAST_CLUSTER_IN_CHAIN && logicalClusterIndex != FAT12_DEFECTIVE_CLUSTER)
    {
        // pointer to physical sector
        char *bufferPtr = buffer;
        bufferPtr += (dataAreaOffsetInBytes + logicalClusterIndex * bpb->bytesPerSec);

        // cast to directory entry
        directory_entry *directoryEntryPtr = (directory_entry *)bufferPtr;

        // output all entries
        bool returnLinks = true;
        int entriesUsed = iterateEntries(directoryEntryPtr, DIR_ENTRIES_PER_SECTOR, returnLinks);
        if (entriesUsed > 0)
        {
            lastUsedLogicalSector = logicalClusterIndex;
        }

        // read next sector in the chain of sectors from the fat
        logicalClusterIndex = readFAT12Entry(buffer, firstFatOffset, logicalClusterIndex);
    }

    // update the FAT and remove unused sectors
    logicalClusterIndex = directoryEntry->first_logical_cluster;
    int oldClusterIndex = logicalClusterIndex;
    bool lastSectorFound = false;
    while (logicalClusterIndex > 1 && logicalClusterIndex != FAT12_LAST_CLUSTER_IN_CHAIN && logicalClusterIndex != FAT12_DEFECTIVE_CLUSTER)
    {
        // read next sector in the chain of sectors from the fat
        oldClusterIndex = logicalClusterIndex;
        logicalClusterIndex = readFAT12Entry(buffer, firstFatOffset, logicalClusterIndex);

        if (oldClusterIndex == lastUsedLogicalSector)
        {
            for (int i = 0; i < bpb->numFats; i++)
            {
                writeFAT12Entry(buffer, fatOffset(bpb, i), oldClusterIndex, FAT12_LAST_CLUSTER_IN_CHAIN);
            }
            lastSectorFound = true;
            continue;
        }

        if (lastSectorFound)
        {
            for (int i = 0; i < bpb->numFats; i++)
            {
                writeFAT12Entry(buffer, fatOffset(bpb, i), oldClusterIndex, FAT12_FREE_CLUSTER);
            }
        }
    }
}

/**
 * Deletes a file
 * 
 * 0. Check if the file exists and is not read only in its directory entries attributes.
 * 
 * Set the directory entry to free (0xE5 as first byte in the filename)
 * if there are no used directory entries after this one, set the first byte in the filename to 0x00
 * 
 * If the cluster is contained in the data area not in the root directory
 * and the entire cluster consist only of empty directory entries, remove this cluster from the
 * folders cluster chain, if it is the last cluster in the cluster chain!
 * 
 * Delete the entire cluster chain of the file.
 */
void rm(const char *buffer, const bios_parameter_block *bpb, const char *filename)
{
    directory_entry *directoryEntry = findFile(buffer, bpb, filename);
    if (directoryEntry == NULL)
    {
        return;
    }

    // check readonly and volume label
    if (directoryEntry->attributes == VOLUMELABEL_FLAG || directoryEntry->attributes == READONLY_FLAG)
    {
        return;
    }

    uint16_t firstFatOffset = fatOffset(bpb, 0);
    int logicalClusterIndex = directoryEntry->first_logical_cluster;
    int oldLogicalClusterIndex = logicalClusterIndex;

    while (logicalClusterIndex > 1 && logicalClusterIndex != FAT12_LAST_CLUSTER_IN_CHAIN && logicalClusterIndex != FAT12_DEFECTIVE_CLUSTER)
    {
        oldLogicalClusterIndex = logicalClusterIndex;

        // read next sector in the chain of sectors from the fat
        logicalClusterIndex = readFAT12Entry(buffer, firstFatOffset, logicalClusterIndex);

        for (int i = 0; i < bpb->numFats; i++)
        {
            writeFAT12Entry(buffer, fatOffset(bpb, i), oldLogicalClusterIndex, FAT12_FREE_CLUSTER);
        }
    }

    // erase directory entry
    memset(directoryEntry, 0, sizeof(directory_entry));

    // set entry to unused
    directoryEntry->filename[0] = DIRECTORY_ENTRY_FREE;

    // collapse the folder
    collapseTheFolder(buffer, bpb, workingDirectory);
}

int main()
{
    printf("Starting the application\n");

    char in[8] = "testtes\0";
    char out[8];
    int maxLength = 8;
    char *buffer = NULL;

    if (load_file_to_memory("resources/jamesmol.img", &buffer) < 0)
    //if (load_file_to_memory("resources/msdos_disk1.img", &buffer) < 0)
    {
        printf("Loading the file failed!\n");

        return -1;
    }

    printf("File loaded!\n");

    bios_parameter_block *bpb = (bios_parameter_block *)buffer;

    //outputFat(buffer, bpb);

    if (bpb->bytesPerSec <= 0 || bpb->rsvdSecCnt <= 0 || bpb->numFats <= 0) 
    {
        printf("Not a FAT12 image!\n");

        free(buffer);
        buffer = NULL;

        return 0;
    }

    // compute the sector where the first FAT starts
    int fatStartSector = bpb->rsvdSecCnt;

    // compute the amount of sectors that the redundant FAT information occupies
    // number of redundant copies of the FAT table times the amount of sectors one FAT occupies
    int fatSectors = bpb->secPerFat * bpb->numFats;

    // compute the sector on which the root dir starts
    int rootDirStartSector = fatStartSector + fatSectors;

    // compute the amount of sectors the root dir occupies
    int rootDirSectors = (32 * bpb->rootEntCnt + bpb->bytesPerSec - 1) / bpb->bytesPerSec;

    // compute the start sector of the data area
    int dataStartSector = rootDirStartSector + rootDirSectors;

    // CountofClusters from http://elm-chan.org/docs/fat_e.html
    // When the value of bpb->totSec32 on the FAT12/16 volume is less than 0x10000,
    // this field must be invalid value 0 and the true value is set to BPB_TotSec16.
    // On the FAT32 volume, this field is always valid and old field is not used.
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
        printf("\n");

        ls(buffer, bpb);
    }
    else if (countOfClusters <= 65525)
    {
        printf("FAT16\n");
        printf("Not implemented yet!\n");
    }
    else
    {
        printf("FAT32\n");
        printf("Not implemented yet!\n");
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

/*
int main()
{
    printf("Starting the application\n");

    char in[8] = "testtes\0";
    char out[8];
    int maxLength = 8;

    //numericalTruncate(out, "t\0", sizeof(out), maxLength);
    //numericalTruncate(out, "testtes\0", sizeof(out), maxLength);
    //numericalTruncate(out, "testtest\0", sizeof(out), maxLength);
    //numericalTruncate(out, "testRIPtestRIPtest", sizeof(out), maxLength);
    //numericalTruncate(out, "VER_12", sizeof(out), maxLength);

    char *buffer = NULL;

    // load the file
    //if (load_file_to_memory("resources/fat.fs", &buffer) < 0)

    // fat12.img comes from http://alexander.khleuven.be/courses/bs1/fat12/fat12.html

    // http://dk.toastednet.org/FLOPPY/

    // https://archive.org/details/floppysoftware?&sort=-downloads&page=2

    //if (load_file_to_memory("resources/badfloppy1.img", &buffer) < 0)
    //if (load_file_to_memory("resources/badfloppy2.img", &buffer) < 0)
    //if (load_file_to_memory("resources/BOOTDSK.IMA", &buffer) < 0)
    //if (load_file_to_memory("resources/BS_PS_1.IMA", &buffer) < 0)
    //if (load_file_to_memory("resources/BS_PS_2.IMA", &buffer) < 0)
    //if (load_file_to_memory("resources/CORR7_1.IMA", &buffer) < 0)
    //if (load_file_to_memory("resources/CORR7_2.IMA", &buffer) < 0)
    if (load_file_to_memory("resources/fat.fs", &buffer) < 0)
    //if (load_file_to_memory("resources/fat12.img", &buffer) < 0)
    //if (load_file_to_memory("resources/floppy.img", &buffer) < 0)
    //if (load_file_to_memory("resources/floppy2.img", &buffer) < 0)
    //if (load_file_to_memory("resources/keen6-1.0-cga-ega-disk1.img", &buffer) < 0)
    //if (load_file_to_memory("resources/msdos_disk1.img", &buffer) < 0)
    //if (load_file_to_memory("resources/msdos_disk2.img", &buffer) < 0)
    //if (load_file_to_memory("resources/msdos_disk3.img", &buffer) < 0)
    {
        printf("Loading the file failed!\n");
        return -1;
    }

    //uint16_t value = ((uint16_t)(buffer[12]) << 8) | (uint16_t)buffer[11];

    bios_parameter_block *bpb = (bios_parameter_block *)buffer;

   
    //// convert endianess
    //bpb->bytesPerSec = __bswap_16(bpb->bytesPerSec);
    //bpb->rsvdSecCnt = __bswap_16(bpb->rsvdSecCnt);
    //bpb->rootEntCnt = __bswap_16(bpb->rootEntCnt);
    //bpb->totSec16 = __bswap_16(bpb->totSec16);
    //bpb->fatSz16 = __bswap_16(bpb->fatSz16);
    //bpb->secPerTrk = __bswap_16(bpb->secPerTrk);
    //bpb->numHeads = __bswap_16(bpb->numHeads);
    //bpb->hiddSec = __bswap_32(bpb->hiddSec);
    //bpb->totSec32 = __bswap_32(bpb->totSec32);

    // compute the sector where the first FAT starts
    int fatStartSector = bpb->rsvdSecCnt;

    // compute the amount of sectors that the redundant FAT information occupies
    // number of redundant copies of the FAT table times the amount of sectors one FAT occupies
    int fatSectors = bpb->secPerFat * bpb->numFats;

    // compute the sector on which the root dir starts
    int rootDirStartSector = fatStartSector + fatSectors;

    // compute the amount of sectors the root dir occupies
    int rootDirSectors = (32 * bpb->rootEntCnt + bpb->bytesPerSec - 1) / bpb->bytesPerSec;

    // compute the start sector of the data area
    int dataStartSector = rootDirStartSector + rootDirSectors;

    // CountofClusters from http://elm-chan.org/docs/fat_e.html
    // When the value of bpb->totSec32 on the FAT12/16 volume is less than 0x10000,
    // this field must be invalid value 0 and the true value is set to BPB_TotSec16.
    // On the FAT32 volume, this field is always valid and old field is not used.
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
        printf("\n");

        // output all entries in the first fat table
        //outputFat(buffer, bpb);
        //writeFAT12Entry(buffer, fatOffset(bpb, 0), 2, 3);
        //outputFat(buffer, bpb);

        //int16_t freeLogicalIndex = findFreeLogicalCluster(buffer, bpb);

        // ls(buffer, bpb);
        // mkdir(buffer, bpb, "folder3");
        // //rmdir(buffer, bpb, "folder3");
        // cd(buffer, bpb, "folder3");
        // touch(buffer, bpb, "file.txt", NULL);
        // cd(buffer, bpb, "..");
        // rmdir(buffer, bpb, "folder3");
        // cd(buffer, bpb, "folder3");
        // rm(buffer, bpb, "file.txt");
        // cd(buffer, bpb, "..");
        // rmdir(buffer, bpb, "folder3");
        // cd(buffer, bpb, "folder3");

        outputFat(buffer, bpb);
        mkdir(buffer, bpb, "folder4");
        outputFat(buffer, bpb);
        cd(buffer, bpb, "folder4");
        touch(buffer, bpb, "file1.txt", NULL);
        touch(buffer, bpb, "file2.txt", NULL);
        touch(buffer, bpb, "file3.txt", NULL);
        touch(buffer, bpb, "file4.txt", NULL);
        touch(buffer, bpb, "file5.txt", NULL);
        touch(buffer, bpb, "file6.txt", NULL);
        touch(buffer, bpb, "file7.txt", NULL);
        touch(buffer, bpb, "file8.txt", NULL);
        touch(buffer, bpb, "file9.txt", NULL);
        touch(buffer, bpb, "file10.txt", NULL);
        touch(buffer, bpb, "file11.txt", NULL);
        touch(buffer, bpb, "file12.txt", NULL);
        touch(buffer, bpb, "file13.txt", NULL);
        touch(buffer, bpb, "file14.txt", NULL);
        // new sector for the folder
        touch(buffer, bpb, "file15.txt", NULL);
        touch(buffer, bpb, "file16.txt", NULL);
        touch(buffer, bpb, "file17.txt", NULL);
        touch(buffer, bpb, "file18.txt", NULL);
        touch(buffer, bpb, "file19.txt", NULL);
        touch(buffer, bpb, "file20.txt", NULL);
        touch(buffer, bpb, "file21.txt", NULL);
        touch(buffer, bpb, "file22.txt", NULL);
        touch(buffer, bpb, "file23.txt", NULL);
        touch(buffer, bpb, "file24.txt", NULL);
        touch(buffer, bpb, "file25.txt", NULL);
        touch(buffer, bpb, "file26.txt", NULL);
        touch(buffer, bpb, "file27.txt", NULL);
        touch(buffer, bpb, "file28.txt", NULL);
        touch(buffer, bpb, "file29.txt", NULL);
        touch(buffer, bpb, "file30.txt", NULL);
        // new sector
        touch(buffer, bpb, "file31.txt", NULL);
        outputFat(buffer, bpb);
        ls(buffer, bpb);
        rm(buffer, bpb, "file15.txt");
        rm(buffer, bpb, "file16.txt");
        rm(buffer, bpb, "file17.txt");
        rm(buffer, bpb, "file18.txt");
        rm(buffer, bpb, "file19.txt");
        rm(buffer, bpb, "file20.txt");
        rm(buffer, bpb, "file21.txt");
        rm(buffer, bpb, "file22.txt");
        rm(buffer, bpb, "file23.txt");
        rm(buffer, bpb, "file24.txt");
        rm(buffer, bpb, "file25.txt");
        rm(buffer, bpb, "file26.txt");
        rm(buffer, bpb, "file27.txt");
        rm(buffer, bpb, "file28.txt");
        rm(buffer, bpb, "file29.txt");
        rm(buffer, bpb, "file30.txt");
        ls(buffer, bpb);

        outputFat(buffer, bpb);
        rm(buffer, bpb, "file31.txt");
        outputFat(buffer, bpb);

        ls(buffer, bpb);
        mkdir(buffer, bpb, "folder1");
        ls(buffer, bpb);
        cd(buffer, bpb, "folder1");
        ls(buffer, bpb);

        outputFat(buffer, bpb);

        touch(buffer, bpb, "file.txt", NULL);
        //touch(buffer, bpb, "file.txt", NULL);
        ls(buffer, bpb);

        outputFat(buffer, bpb);

        const char data5[514];
        memset(data5, 'y', 514);
        int bytesWritten = appendToFile(buffer, bpb, "file.txt", data5, 514);
        outputFileByName(buffer, bpb, "file.txt");

        outputFat(buffer, bpb);

        rm(buffer, bpb, "file.txt");
        ls(buffer, bpb);

        outputFat(buffer, bpb);

        // const char *data = "test";
        // int bytesWritten = appendToFile(buffer, bpb, "file.txt", data, strlen(data));
        // outputFileByName(buffer, bpb, "file.txt");

        // const char *data2 = "aaaaaa";
        // bytesWritten = appendToFile(buffer, bpb, "file.txt", data2, strlen(data2));
        // outputFileByName(buffer, bpb, "file.txt");

        // const char data3[510];
        // memset(data3, 'x', 510);
        // bytesWritten = appendToFile(buffer, bpb, "file.txt", data3, 510);
        // outputFileByName(buffer, bpb, "file.txt");

        // const char *data4 = "aaaaaaaaaa";
        // bytesWritten = appendToFile(buffer, bpb, "file.txt", data4, strlen(data4));
        // outputFileByName(buffer, bpb, "file.txt");

        // const char data5[514];
        // memset(data5, 'y', 514);
        // bytesWritten = appendToFile(buffer, bpb, "file2.txt", data5, 514);
        // outputFileByName(buffer, bpb, "file2.txt");

        ls(buffer, bpb);
        mkdir(buffer, bpb, "folder1");
        ls(buffer, bpb);
        mkdir(buffer, bpb, "folder2");
        ls(buffer, bpb);
        mkdir(buffer, bpb, "folder3");
        mkdir(buffer, bpb, "folder4");
        mkdir(buffer, bpb, "folder5");
        mkdir(buffer, bpb, "folder6");
        mkdir(buffer, bpb, "folder7");
        mkdir(buffer, bpb, "folder8");
        mkdir(buffer, bpb, "folder9");
        mkdir(buffer, bpb, "folder10");
        ls(buffer, bpb);
        mkdir(buffer, bpb, "folder11");
        mkdir(buffer, bpb, "folder12");
        mkdir(buffer, bpb, "folder13");
        mkdir(buffer, bpb, "folder14");
        mkdir(buffer, bpb, "folder15");
        mkdir(buffer, bpb, "folder16");
        ls(buffer, bpb);
        mkdir(buffer, bpb, "folder17");
        ls(buffer, bpb);
        cd(buffer, bpb, "..");
        ls(buffer, bpb);
        cd(buffer, bpb, "..");

        //directory_entry *freeDirEntry = findFreeDirEntry(buffer, bpb, workingDirectory);
        //cd(buffer, bpb, "TESTDIR");

        //appendClusterSectorToChain(buffer, bpb, workingDirectory);

        //directory_entry *freeDirEntry = findFreeDirEntry(buffer, bpb, workingDirectory);

        //mkdir(buffer, bpb, "folder1");

        //cd(buffer, bpb, "TESTDIR");
        //cd(buffer, bpb, "BIN");
        //cd(buffer, bpb, "autoexec.bat");
        //cd(buffer, bpb, "PSTRIKE._1");
        //ls(buffer, bpb);

        //outputFileByName(buffer, bpb, "info.txt");
        outputFileByName(buffer, bpb, "autoexec.bat");
        //outputFileByName(buffer, bpb, "qbasic.exe");

        ls(buffer, bpb);
        cd(buffer, bpb, "testdir");
        ls(buffer, bpb);
        cd(buffer, bpb, "..");
        ls(buffer, bpb);
        cd(buffer, bpb, "..");

        cd(buffer, bpb, "BIN");
        ls(buffer, bpb);

        cd(buffer, bpb, "DRIVERS");
        ls(buffer, bpb);

        cd(buffer, bpb, "..");
        ls(buffer, bpb);

        cd(buffer, bpb, "RFC3940.TXT");

        // "  ..test..  " --> "TEST.."
        cd(buffer, bpb, "  ..test..  ");

        // "  ..testtesttest..  " --> "TESTTE~1"
        cd(buffer, bpb, "  ..testtesttest..  ");

        //cd(buffer, bpb, "..testtesttest..");

        cd(buffer, bpb, "  ..aaaaaaaa..  ");

        // ".test" --> "TEST"
        cd(buffer, bpb, ".test");

        // "test." --> "TEST."
        cd(buffer, bpb, "test.");

        cd(buffer, bpb, "ver +1.2.text");

        // "test.txt" --> "TEST.TXT" --> TEST    .TXT
        cd(buffer, bpb, "test.txt");

        // "information1234.txt" --> "INFORM~1TXT" --> "INFORM~1TXT"
        cd(buffer, bpb, "information1234.txt");

        // "information1234.testor" --> "INFORM~1TES" --> "INFORM~1TES"
        cd(buffer, bpb, "information1234.testor");

        // "TextFile.Mine.txt" --> "TEXTFI~1TXT" --> "TEXTFI~1TXT"
        cd(buffer, bpb, "TextFile.Mine.txt");

        // "notexistaa" --> "NOTEXI~1"
        cd(buffer, bpb, "notexistaa");

        cd(buffer, bpb, "NOTEXI~1");

        // "notexist" --> "NOTEXIST"
        cd(buffer, bpb, "notexist");

        ls(buffer, bpb);
        cd(buffer, bpb, "DRAFTS");
        ls(buffer, bpb);
        cd(buffer, bpb, "..");
        ls(buffer, bpb);
        cd(buffer, bpb, "..");

        // put pointer on the root directory
        // the organization of a FAT12 system consists of four blocks.
        // Their position (sector at which they start) is predefined. (See https://www.google.com/url?sa=t&rct=j&q=&esrc=s&source=web&cd=4&ved=2ahUKEwjolfKslrLjAhVCcZoKHcUMDPwQFjADegQIBRAC&url=http%3A%2F%2Fwww.disc.ua.es%2F~gil%2FFAT12Description.pdf&usg=AOvVaw0vfkjD-j5QnMsNIWTxKuzR)
        //
        // The four blocks and their start sectors are
        // 1. Boot Sector (StartSector: 0)
        // 2. Two FAT Tables (StartSector Table 1: 1, StartSector Table 2: 10)
        // 3. The root directory (StartSector: 19)
        // 4. Data Area (StartSector: 33)

        // To convert from a sector number (e.g. 19 for the root directory) to an offset in bytes
        // the computation is sector size in bytes * startsector index

        //int rootDirectoryOffset;

        
        // compute the sector where the root directory starts
        // reserved Sector count tells us how many sectors are reserved for boot information
        // secPerFat contains the sectors used for each FAT table
        // numFats is the amount of copies of the FAT. For crash-safetry, the FAT is duplicated to have it redundand
        // Copies of the FAT are still available even if one of the copies is corrupted.
        int rootDirectoryOffsetInSectors = bpb->rsvdSecCnt + bpb->secPerFat * bpb->numFats;

        // convert the offset in sectors to an offset in bytes
        int rootDirectoryOffsetInBytes = rootDirectoryOffsetInSectors * bpb->bytesPerSec;

        char *ptr = buffer;
        ptr += rootDirectoryOffsetInBytes;

        directory_entry *dirEntry = (directory_entry *)ptr;

        outputRootFolder(buffer, bpb);

        directory_entry *dirEntryIter = dirEntry;
        for (int i = 0; i < bpb->rootEntCnt; i++)
        {
            // skip empty entries
            // If the first byte of the Filename field is 0xE5, then the directory entry is free
            // (i.e., currently unused), and hence there is no file or subdirectory associated with the directory entry.
            //
            // If the first byte of the Filename field is 0x00, then this directory entry is free and all
            // the remaining directory entries in this directory are also free.
            if (dirEntryIter->filename[0] == 0xE5)
            {
                // go to the next entry
                dirEntryIter++;
                continue;
            }
            else if (dirEntryIter->filename[0] == 0x00)
            {
                // break the loop, because all subsequent entries are free too
                break;
            }

            //outputDirectoryEntry(dirEntryIter);

            if (isFile(dirEntryIter))
            {
                // // compute physical sector
                // int16_t physicalSector = logicalToPhysical(bpb, dirEntryIter->first_logical_cluster);
                // // print the physical sector
                // char *dirEntryPtr2 = buffer;
                // dirEntryPtr2 += (physicalSector * bpb->bytesPerSec);
                // printf("%.512s\n", dirEntryPtr2);

                //outputFile(buffer, bpb, dirEntryIter->first_logical_cluster);
            }
            else if (isDirectory(dirEntryIter))
            {
                outputFolder(buffer, bpb, dirEntryIter->first_logical_cluster);
            }

            dirEntryIter++;
        }
         
        // back to the first entry of the root directory
        dirEntryIter = dirEntry;

        // go to the second entry (which is a file)
        dirEntryIter++;
        

        // // physical sector number = 33 + FAT entry number - 2
        // // a physical sector is just a sector on the volume
        // // the first physical sector is the boot sector
        // // the following sectors are part of reserved sectors or maybe the FAT tables
        // // Followed by sectors for the root directory
        // // Followed by sectors for the data area.
        // //
        // // Converting the logical sector of a directory entry into a physical sector
        // // will give you a sector in the data area that contains that file or directory
        // //
        // // Q: Why subtract 2?
        // // A: The cluster numbers are linear and are relative to the data area, with cluster 2 being the first cluster of the data area
        // int16_t physicalSector = bpb->rsvdSecCnt + (bpb->secPerFat * bpb->numFats) + rootDirSectors + dirEntryIter->first_logical_cluster - 2; // physical sector 2204

        // // pointer to sector that contains the first root directory directory entry
        // char *dirEntryPtr = buffer;
        // dirEntryPtr += (physicalSector * bpb->bytesPerSec);

        // //        printf("%.512s\n", dirEntryPtr);

        // // next cluster
        // physicalSector++; // physical sector 2205
        // dirEntryPtr = buffer;
        // dirEntryPtr += (physicalSector * bpb->bytesPerSec);
        // //       printf("%.512s\n", dirEntryPtr);

        // //     printf("test\n");

        // // get pointer to beginning of first fat
        // int fatOffset = bpb->rsvdSecCnt;
        // fatOffset *= bpb->bytesPerSec;
        // char *fatptr = buffer;
        // fatptr += fatOffset;

        // int fatSizeInBytes = bpb->secPerFat * bpb->bytesPerSec;

        // // 2174
        // // offset: 3770 - first[1064]: 0 - second[1065]: 2174
        // offset: 3770 - first[2172]: 0 - second[2173]: 2174
        // offset: 3773 - first[2174]: 2175 - second[2175]: 2176
        // offset: 3776 - first[2176]: 2177 - second[2177]: 2178

        // //int n = 2174;
        // int n = 2175;
        // int fatindex1 = -1;
        // int fatindex2 = -1;
        // if (n % 2)
        // {
        //     // odd
        //     fatindex1 = (3 * n) / 2 + 512;
        //     fatindex2 = fatindex1 + 1;
        // }
        // else
        // {
        //     // even
        //     fatindex2 = (3 * n) / 2 + 512;
        //     fatindex1 = fatindex2 + 1;
        // }

        // int entryIdx = -1;
        // // every three bytes in the fat contain two entries
        // for (int i = 0; i < (fatSizeInBytes / 3 * 2); i++)
        // {
        //     int value = readFAT12Entry(buffer, fatOffset, i);
        //     printf("entry: %d value: %d\n", i, value);
        // }

        // output all entries in the first fat table
        //outputFat(buffer, bpb);

        /*
        int entryIdx = -1;
        for (int i = 0; i < fat32SizeInBytes; i += 3)
        {
            
            unsigned char a = fatptr[i];
            unsigned char b = fatptr[i + 1];
            unsigned char c = fatptr[i + 2];

            uint32_t fatTouple = (c << 16) + (b << 8) + a;

            if (fatTouple == 0)
            {
                printf("offset: %d - first[%d]: %d - second[%d]: %d\n", i + fatOffset, ++entryIdx, 0, ++entryIdx, 0);

                continue;
            }

            uint32_t secondLogicalSector = fatTouple & 0xFFF000;
            secondLogicalSector >>= 12;
            uint32_t firstLogicalSector = fatTouple & 0x000FFF;

            printf("offset: %d - first[%d]: %d - second[%d]: %d\n", i + fatOffset, ++entryIdx, firstLogicalSector, ++entryIdx, secondLogicalSector);
             

            // entryIdx++;
            // if (entryIdx == 2204)
            // {
            //     printf("test");
            // }

            // if (firstLogicalSector >= 0x002 && firstLogicalSector <= 0xFEF)
            // {
            //     int16_t tempPhysicalSector = bpb->rsvdSecCnt + (bpb->secPerFat * bpb->numFats) + rootDirSectors;
            //     //tempPhysicalSector += (secondLogicalSector - 2) * bpb->secPerClus;
            //     tempPhysicalSector += (secondLogicalSector - 2);
            //     printf("%d\n", tempPhysicalSector);
            //     char *dirEntryPtr2 = buffer;
            //     dirEntryPtr2 += (tempPhysicalSector * bpb->bytesPerSec);
            //     printf("%.512s\n", dirEntryPtr2);
            // }

            // entryIdx++;
            // if (entryIdx == 2204)
            // {
            //     printf("test");
            // }

            // if (secondLogicalSector >= 0x002 && secondLogicalSector <= 0xFEF)
            // {
            //     //secondLogicalSector = 2173;
            //     int16_t tempPhysicalSector = bpb->rsvdSecCnt + (bpb->secPerFat * bpb->numFats) + rootDirSectors;
            //     //tempPhysicalSector += (secondLogicalSector - 2) * bpb->secPerClus;
            //     tempPhysicalSector += (secondLogicalSector - 2);
            //     printf("%d\n", tempPhysicalSector);
            //     char *dirEntryPtr2 = buffer;
            //     dirEntryPtr2 += (tempPhysicalSector * bpb->bytesPerSec);
            //     printf("%.512s\n", dirEntryPtr2);
            // }
        }


        printf("done");

        //int nextValue1 = readFAT12Entry(buffer, fatOffset, 2174);
        //int nextValue2 = readFAT12Entry(buffer, fatOffset, 2175);

        //int nextValue3 = readFAT12Entry(buffer, fatOffset, 1450);
        //int nextValue4 = readFAT12Entry(buffer, fatOffset, 1451);

        // The cluster IDs that we have been using are logical clusters,
        // meaning that they represent the cluster number starting from the beginning of the data section.
        // The numbers 0 and 1 have a special meaning and are not used as cluster IDs.
        // So the first cluster of the data section has cluster ID 2.
        // If we want to know where exactly a logical cluster is stored on the disk, we need to convert
        // it into a physical cluster.

        // We have already calculated that there are 33 clusters before the start of the data section
        // (
        //    1 boot cluster,                    // bpb->rsvdSecCnt
        //    9 clusters for each FAT,           // bpb->secPerFat * bpb->numFats / bpb->secPerClus
        //    14 clusters for the root directory // bpb->rootEntCnt *
        // ).
        // Using this information,
        // and knowing that the first valid cluster ID is 2, we can deduce the following formula to calculate
        // the physical cluster ID from a given logical cluster ID: physicalID = 33 + logicalID - 2.

        // Once we know the physical cluster ID, we can calculate the physical
        // location of that cluster on the disk by multiplying the ID with the cluster size (512 bytes).

        //uint16_t clusterIndex = logicalCluster

        // physical sector number = 33 + FAT entry number - 2

               int fatOffset = bpb->rsvdSecCnt;

        // convert the offset in sectors to an offset in bytes
        fatOffset *= bpb->bytesPerSec;

        char *fatptr = buffer;
        fatptr += fatOffset;

        printf("%d\n", *fatptr);
        printf("%d\n", *(fatptr + 1));
        printf("%d\n", *(fatptr + 1));

        // every three bytes contain two fat table entries, because a fat table entry is 12 bits
        int16_t fatTableEntryIndex = dirEntryIter->first_logical_cluster / 2;

        // find the index where this three byte touple starts in the buffer
        fatTableEntryIndex *= 3;

        int fatTableEntryModulo = dirEntryIter->first_logical_cluster % 2;

        //uint32_t fatTouple = (fatptr[fatTableEntryIndex] << 16) + (fatptr[fatTableEntryIndex + 1] << 8) + fatptr[fatTableEntryIndex + 2];
        uint32_t fatTouple = (fatptr[fatTableEntryIndex + 2] << 16) + (fatptr[fatTableEntryIndex + 1] << 8) + fatptr[fatTableEntryIndex];

        uint32_t firstLogicalSector = fatTouple & 0xFFF000;

        uint32_t secondLogicalSector = fatTouple & 0x000FFF;

        uint32_t logicalSector = -1;
        if (fatTableEntryModulo)
        {
            printf("%d\n", firstLogicalSector);
            logicalSector = firstLogicalSector;
        }
        else
        {
            printf("%d\n", secondLogicalSector);
            logicalSector = secondLogicalSector;
        }

        uint32_t physicalSector2 = 33 + logicalSector - 2;

        dirEntryPtr = buffer;
        dirEntryPtr += (physicalSector2 * bpb->bytesPerSec);

        printf("%.512s\n", dirEntryPtr);
         
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
*/