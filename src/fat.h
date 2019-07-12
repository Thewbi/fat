#ifndef FAT_H
#define FAT_H

#include <inttypes.h>
#include <byteswap.h>

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
    uint8_t secPerClus; // Logical sectors per cluster
    uint16_t rsvdSecCnt; // Reserved logical sectors 
    uint8_t numFats; // Number of FATs 
    uint16_t rootEntCnt; // Root directory entries 
    uint16_t totSec16; // Total logical sectors 
    int8_t media; // Media descriptor 
    int16_t fatSz16; // Logical sectors per FAT 
    int16_t secPerTrk;
    int16_t numHeads;
    int32_t hiddSec;
    int32_t totSec32;

} bios_parameter_block ;

#endif