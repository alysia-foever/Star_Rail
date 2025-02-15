#ifndef FAT16_H
#define FAT16_H

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define FUSE_USE_VERSION 31
#include <fuse.h>

/* Unit size */
#define PHYSICAL_SECTOR_SIZE 512        // Size of Physical Sector
#define MAX_LOGICAL_SECTOR_SIZE 4096    // Maximum size of logical sector
#define SEC_PER_TRACK  512              // Number of tracks per physical sector (Only used to simulate hard disk)
#define DIR_ENTRY_SIZE 32               // The size of directory entry

// File attributes, please refer to https://en.wikipedia.org/wiki/Design_of_the_FAT_file_system#DIR_OFS_0Bh
#define ATTR_NONE           0x00
#define ATTR_READONLY       0x01        // Read Only (只读)
#define ATTR_HIDDEN         0x02        // Hidden (隐藏)
#define ATTR_SYSTEM         0x04        // System (系统)
#define ATTR_VOLUME         0x08        // Volume (卷标)
#define ATTR_DIRECTORY      0x10        // Directory (目录)
#define ATTR_ARCHIVE        0x20        // Archive (归档)
#define ATTR_LFN            0x0F        // Long File Name (长文件名项)

#define ATTR_REGULAR        0x20        // Regular File

#define NAME_DELETED        0xE5        // Deleted (被删除的项)
#define NAME_FREE           0x00        // Free (未使用的项)

// 用户权限
#define S_IRUGO   (S_IRUSR | S_IRGRP | S_IROTH)
#define S_IXUGO   (S_IXUSR | S_IXGRP | S_IXOTH)
#define S_RDONLY  S_IRUGO                           // Read-only: 444
#define S_NORMAL  (S_IRUGO | S_IWUSR | S_IXUGO)     // Regular file: 755

#define FAT_NAME_LEN 11
#define FAT_NAME_BASE_LEN 8
#define FAT_NAME_EXT_LEN 3

#define MAX_NAME_LEN 512

typedef uint16_t cluster_t;
typedef uint64_t sector_t;
typedef uint8_t attr_t;


typedef uint8_t BYTE;
typedef uint16_t WORD;
typedef uint32_t DWORD;

// Cluster Number (FAT Table Entry)
#define CLUSTER_FREE        0x0000u         // Free Cluster (未分配的簇号)
#define CLUSTER_MIN         0x0002u         // First Possible Cluster (第一个代表簇的簇号)
#define CLUSTER_MAX         0xFFEFu         // Last Possible Cluster (最后一个代表簇的簇号)
#define CLUSTER_END         0xFFFFu         // Ending Cluster (文件结束的簇号)
#define CLUSTER_END_BOUND   0xFFF8u         // End Bound (文件结束簇号界限，大于等于该数的簇号都被视为文件结束)

#define DEFAULT_IMAGE       "fat16.img"

#define min(x, y) (((x) < (y)) ? (x) : (y))
#define max(x, y) (((x) > (y)) ? (x) : (y))

/* FAT16 BPB Structure */
typedef struct {
    BYTE BS_jmpBoot[3];
    BYTE BS_OEMName[8];
    WORD BPB_BytsPerSec;
    BYTE BPB_SecPerClus;
    WORD BPB_RsvdSecCnt;
    BYTE BPB_NumFATS;
    WORD BPB_RootEntCnt;
    WORD BPB_TotSec16;
    BYTE BPB_Media;
    WORD BPB_FATSz16;
    WORD BPB_SecPerTrk;
    WORD BPB_NumHeads;
    DWORD BPB_HiddSec;
    DWORD BPB_TotSec32;
    BYTE BS_DrvNum;
    BYTE BS_Reserved1;
    BYTE BS_BootSig;
    DWORD BS_VollID;
    BYTE BS_VollLab[11];
    BYTE BS_FilSysType[8];
    BYTE Reserved2[448];
    WORD Signature_word;
} __attribute__ ((packed, aligned(__alignof__(DWORD)))) BPB_BS;
static_assert(sizeof(BPB_BS) == PHYSICAL_SECTOR_SIZE, "Wrong BPB size");

/* FAT Directory Structure */
typedef struct {
    BYTE DIR_Name[11];
    BYTE DIR_Attr;
    BYTE DIR_NTRes;
    BYTE DIR_CrtTimeTenth;
    WORD DIR_CrtTime;
    WORD DIR_CrtDate;
    WORD DIR_LstAccDate;
    WORD DIR_FstClusHI;
    WORD DIR_WrtTime;
    WORD DIR_WrtDate;
    WORD DIR_FstClusLO;
    DWORD DIR_FileSize;
} __attribute__ ((packed, aligned(__alignof__(DWORD)))) DIR_ENTRY;
static_assert(sizeof(DIR_ENTRY) == DIR_ENTRY_SIZE, "Wrong DIR_ENTRY size");

enum FindResult {
    FIND_EXIST = 0,
    FIND_EMPTY = 1,
    FIND_FULL  = 2
};

int sector_read(sector_t sec_num, void *buffer);
int sector_write(sector_t sec_num, const void *buffer);

#endif
