#ifndef FAT_H
#define FAT_H

#include <inttypes.h>
//#include <byteswap.h>
#include <stdbool.h>
#include <stdlib.h>
#include <ctype.h>

#define DIR_ENTRIES_PER_SECTOR 16
#define FAT12_DEFECTIVE_CLUSTER 0xFF7
#define FAT12_LAST_CLUSTER_IN_CHAIN 0xFFF
#define FAT12_FREE_CLUSTER 0x000

#define FILENAME_LENGTH 11
//#define FILENAME_LENGTH 12

#define READONLY_FLAG 0x01
#define HIDDEN_FLAG 0x02
#define SYSTEM_FLAG 0x04
#define VOLUMELABEL_FLAG 0x08
#define DIRECTORY_FLAG 0x10
#define ARCHIVE_FLAG 0x20
#define UNUSED1_FLAG 0x40
#define UNUSED2_FLAG 0x80

#define DIRECTORY_ENTRY_FREE 0xE5
#define DIRECTORY_ENTRY_LAST 0x00

typedef struct __attribute__((packed))
{
    uint32_t firstEntry;
    uint32_t secondEntry;
} fat_12_entry;

// http://elm-chan.org/docs/fat_e.html
// https://os.mbed.com/users/fpucher/code/HIM0Board/wiki/Datentypen
// https://de.wikipedia.org/wiki/BIOS_Parameter_Block
// https://jdebp.eu/FGA/bios-parameter-block.html -
//
typedef struct __attribute__((packed))
{
    unsigned char jmpBoot[3];
    unsigned char oemName[8];
    uint16_t bytesPerSec; // Bytes per logical sector
    uint8_t secPerClus;   // Logical sectors per cluster
    uint16_t rsvdSecCnt;  // Reserved logical sectors
    uint8_t numFats;      // Number of FATs (FATs are duplicated to guarantee access to the data area even if single copies of the FAT get corrupted)
    uint16_t rootEntCnt;  // Root directory entry count
    uint16_t totSec16;    // Total logical sectors
    int8_t media;         // Media descriptor
    int16_t secPerFat;    // Logical sectors per FAT
    int16_t secPerTrack;
    int16_t numHeads;
    int32_t hiddSec;
    int32_t totSec32;
} bios_parameter_block;

// http://alexander.khleuven.be/courses/bs1/fat12/fat12.html
// http://www.tavi.co.uk/phobos/fat.html#root_directory
// this structure is 32 bytes
typedef struct __attribute__((packed))
{
    unsigned char filename[11]; // the filename and the extension
    int8_t attributes;          // file attributes
    int8_t reserved;            // reserved for Windows NT
    int8_t creation_millis;     // creation - Millsecond stamp (actual 100th of a second)
    int16_t creation_time;
    int16_t creation_date;
    int16_t last_access_date;
    int16_t reserved_for_fat32;
    int16_t last_write_time;
    int16_t last_write_date;
    int16_t first_logical_cluster; // the fat is indexed using logical cluster values
    int32_t filesize;              // filesize in bytes
} directory_entry;

void filenameToFatElevenThree(const char *filename, char *out, int outLen);
void numericalTruncate(char *out, char *input, int outBufferLen, int maxLength);
void to_upper(char *out, char *input, int outBufferLen);

#endif