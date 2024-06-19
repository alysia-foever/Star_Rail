#include <time.h>
#include <assert.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/timeb.h>

#include "fat16.h"
#include "fat16_utils.h"

/* FAT16 volume data with a file handler of the FAT16 image file.
   The data structure of the metadata required by FAT16:
   "Borrowed" from https://elixir.bootlin.com/linux/latest/source/fs/fat/inode.c#L44
  */
typedef struct {
    uint32_t sector_size;      // Logical sector size (bytes)
    uint32_t sec_per_clus;     // Number of sectors per cluster
    uint32_t reserved;         // Number of reserved sectors
    uint32_t fats;             // Number of FAT tables
    uint32_t dir_entries;      // Number of root directory entries
    uint32_t sectors;          // Total number of sectors in the file system
    uint32_t sec_per_fat;      // Number of sectors per FAT table

    sector_t fat_sec;          // Starting sector of the FAT table
    sector_t root_sec;         // Starting sector of the root directory area
    uint32_t root_sectors;     // Number of sectors in the root directory area
    sector_t data_sec;         // Starting sector of the data area

    uint32_t clusters;         // Number of clusters in the file system
    uint32_t cluster_size;     // Cluster size (bytes)

    uid_t fs_uid;              // Can be ignored, user ID mounting the FAT, all files show the owner as this user
    gid_t fs_gid;              // Can be ignored, group ID mounting the FAT, all file user groups show as this group
    struct timespec atime;     // Access time
    struct timespec mtime;     // Modification time
    struct timespec ctime;     // Creation time
} FAT16;

FAT16 meta;

sector_t cluster_first_sector(cluster_t clus) {
    assert(is_cluster_inuse(clus));
    return ((clus - 2) * meta.sec_per_clus) + meta.data_sec;
}

cluster_t sector_cluster(sector_t sec) {
    if(sec < meta.data_sec) {
        return 0;
    }
    cluster_t clus = 2 + (sec - meta.data_sec) / meta.sec_per_clus;
    assert(is_cluster_inuse(clus));
    return clus;
}

cluster_t read_fat_entry(cluster_t clus)
{
    char sector_buffer[MAX_LOGICAL_SECTOR_SIZE];
    /**
     * TASK 4.1
     * TODO:
     *   Read the FAT table entry [~5 lines of code]
     * 
     * Hint:
     *   You need to read the FAT table entry corresponding to `clus` and then 
     *   return the value of that entry.
     * 
     *   - Which sector is the FAT table entry located?
     *   - What is the offset within that sector?
     *   - What is the size of the entry?
     */

    // ================== Your code here =================
    
    //Modified 1
    // 计算FAT表项所在的扇区
    sector_t fat_sector = meta.fat_sec + (clus * 2) / meta.sector_size;
    // 计算FAT表项在扇区内的偏移量
    size_t fat_offset = (clus * 2) % meta.sector_size;

    // 读取FAT表项所在的扇区
    int ret = sector_read(fat_sector, sector_buffer);
    if (ret < 0) {
        return CLUSTER_END;
    }

    // 从扇区缓冲区中提取FAT表项的值
    cluster_t fat_entry_value;
    fat_entry_value = 
        sector_buffer[fat_offset];
    
    
    // ===================================================
    return *((cluster_t*)&sector_buffer[fat_offset]);    // TODO: Remember to delete or modify this line.
}

typedef struct {
    DIR_ENTRY dir;
    sector_t sector;
    size_t offset;
} DirEntrySlot;

/**
 * @brief Find a directory entry, starting from the name for the first len bytes to search for the file/directory name
 * 
 * @param name  : Pointer to the filename (not necessarily NULL-terminated, hence the `len` parameter is needed)
 * @param len   : Length of the filename
 * @param from_sector : Starting sector to search
 * @param sectors_count : Number of sectors to search
 * @param slot  : Output parameter to store the directory entry and its location (if found)
 * @return <int>: Returns FIND_EXIST if an entry is found; FIND_EMPTY when an empty slot is found; FIND_FULL if all sectors are full (sets errno to negative)
 */
int find_entry_in_sectors(const char* name, size_t len, 
            sector_t from_sector, size_t sectors_count, 
            DirEntrySlot* slot) {
    char buffer[MAX_LOGICAL_SECTOR_SIZE];

    /**
     * TASK 2.1
     * TODO:
     *   Find a *directory entry* with the filename to be `name` in the
     *   specified sectors (the `sectors_count` sectors starting from `from_sector`)
     * 
     * Hint:
     *   You can refer to the implementation of `fill_entries_in_sectors`. They
     *   are quite similar. The difference is, you need to find the corresponding
     *   directory entry and assign values to `slot`, then return the correct 
     *   value instead of calling the `filler()` function.
     *   You can use the `check_name()` function to check if the directory entry 
     *   matches the filename you are looking for (`name`)
     *   
     * Note:
     *   - Return `FIND_EXIST` when an entry is found;
     *   - Return `FIND_EMPTY` when an empty slot is found;
     *   - Return `FIND_FULL` when all sectors are full.
     */
    // ================== Your code here =================
    //Modified 1
    for(sector_t sec = from_sector;sec < from_sector + sectors_count;++sec){

    int ret = sector_read(sec,buffer);
    if(ret < 0) {
            return -EIO;
    }
    
    for(size_t off = 0; off < meta.sector_size ; off += DIR_ENTRY_SIZE )
    {
        DIR_ENTRY* entry = (DIR_ENTRY*)(buffer + off);
        if(de_is_free(entry)){
            slot->dir = *entry;
            slot->offset = off;
            slot->sector = sec;
            return FIND_EMPTY;
        }
        if(check_name(name,len,entry)){
            slot->dir = *entry;
            slot->offset = off;
            slot->sector = sec;
            return FIND_EXIST;
        }
        
    }
    }
    // =================================================
    return FIND_FULL;
}

/**
 * @brief Find the directory entry for the specified path. If the last path segment does not exist, find an empty slot to create the last segment file/directory.
 * 
 * @param path    : The path to search
 * @param slot    : Output parameter to store the directory entry and its location (if found)
 * @param remains : Output parameter, points to the unlocated part of the `path`. For example, if the path is "a/b/c", and "a" exists but "b" does not, then `remains` will point to "b/c".
 * @return <int>  :  Returns FIND_EXIST if an entry is found; FIND_EMPTY when an empty slot is found; FIND_FULL if all sectors are full (sets errno to negative)
 */
int find_entry_internal(const char* path, DirEntrySlot* slot, const char** remains) {
    *remains = path;
    *remains += strspn(*remains, "/");    // Skip the leading '/'
    
    // Root directory
    sector_t first_sec = meta.root_sec;
    size_t nsec = meta.root_sectors;
    size_t len = strcspn(*remains, "/"); // Length of the filename to search for at the current level
    int state = find_entry_in_sectors(*remains, len, first_sec, nsec, slot);

    // Locate the start of the next level name
    const char* next_level = *remains + len;
    next_level += strspn(next_level, "/");

    // Handling of results and errors from root directory search
    if(state < 0 || *next_level == '\0') {   // Error, or only one level found, return the result directly
        return state;
    }
    if(state != FIND_EXIST) {   // Not the last level yet, still not found
        return -ENOENT;
    }
    if(!attr_is_directory(slot->dir.DIR_Attr)) { // Not the last level and not a directory
        return -ENOTDIR;
    }

    cluster_t clus = slot->dir.DIR_FstClusLO;  // Cluster number of the first cluster of the file
    *remains = next_level;
    while (true) {
        size_t len = strcspn(*remains, "/"); // Length of the filename to search for at the current level
        /**
         * TASK 5.1
         * TODO:
         *   Search for the directory entry in the path of non-root directories [~3 lines of code]
         * 
         * Hint: 
         *   Complete the code below to search for directory entries in paths
         *   of non-root directories. You should use a while loop to iterate 
         *   through the directory entries in the cluster corresponding to the
         *   current directory.
         */
        // ================== Your code here =================
        //Modified 1
         while (is_cluster_inuse(clus)) {
            sector_t sector = cluster_first_sector(clus);
            state = find_entry_in_sectors(*remains, len, sector, meta.sec_per_clus, slot);
            if (state != FIND_FULL) {
                break;
            }
            clus = read_fat_entry(clus);
        }
        
        // ===================================================

        // At this point, `slot` contains the directory entry for the next level

        const char* next_level = *remains + len;
        next_level += strspn(next_level, "/");

        if(state < 0 || *next_level == '\0') {   // Error, or it's the last level, then return directly
            return state;
        }
        if(state != FIND_EXIST) {
            return -ENOENT;
        }
        if(!attr_is_directory(slot->dir.DIR_Attr)) {
            return -ENOTDIR;
        }

        *remains = next_level;          // `remains` points to the start of the next level name
        clus = slot->dir.DIR_FstClusLO; // Switch to the next cluster
    }
    return -EUCLEAN;
}


/**
 * @brief Write the directory entry (stored in `slot`) into the file system.
 *        The main body of this function is in `find_entry_internal()`.
 *        You don't need to modify this function.
 * @param path  : The path to write to
 * @param slot  : Contains the directory entry
 * @return <int>: Return 0 if the directory entry is found; 
 *                Return -ENOENT if the file does not exist;
 *                Return the returned error code of `find_entry_internal()` if negative
 */
int find_entry(const char* path, DirEntrySlot* slot) {
    const char* remains = NULL;
    int ret = find_entry_internal(path, slot, &remains);
    if(ret < 0) {
        return ret;
    }
    if(ret == FIND_EXIST) {
        return 0;
    }
    return -ENOENT;
}

/**
 * @brief Find an empty slot for creating a file/directory corresponding to `path`; Returns error if already exist
 * 
 * @param path 
 * @param slot 
 * @param last_name 
 * @return <int>: Return 0 if an empty slot is found;
 *                Return -EEXIST if file already exists;
 *                Return -ENOSPC if directory already full.
 */
int find_empty_slot(const char* path, DirEntrySlot *slot, const char** last_name) {
    int ret = find_entry_internal(path, slot, last_name);
    if(ret < 0) {
        return ret;
    }
    if(ret == FIND_EXIST) { // File already exists
        return -EEXIST;
    }
    if(ret == FIND_FULL) {  // All slots are full
        return -ENOSPC;
    }
    return 0;
}

mode_t get_mode_from_attr(uint8_t attr) {
    mode_t mode = 0;
    mode |= attr_is_readonly(attr) ? S_IRUGO : S_NORMAL;
    mode |= attr_is_directory(attr) ? S_IFDIR : S_IFREG;
    return mode;
}

/* ================ File System Interface Implementation ================= */

/**
 * @brief File system initialization. DO NOT MODIFY!
 *        However, you can read this function to learn how to use `sector_read()`
 *        to read metadata in the file system.
 * 
 * @param conn 
 * @return <void*>
 */
void *fat16_init(struct fuse_conn_info * conn, struct fuse_config *config) {
    /* Reads the BPB */
    BPB_BS bpb;
    sector_read(0, &bpb);
    meta.sector_size = bpb.BPB_BytsPerSec;
    meta.sec_per_clus = bpb.BPB_SecPerClus;
    meta.reserved = bpb.BPB_RsvdSecCnt;
    meta.fats = bpb.BPB_NumFATS;
    meta.dir_entries = bpb.BPB_RootEntCnt;
    meta.sectors = bpb.BPB_TotSec16 != 0 ? bpb.BPB_TotSec16 : bpb.BPB_TotSec32;
    meta.sec_per_fat = bpb.BPB_FATSz16;


    meta.fat_sec = meta.reserved;
    meta.root_sec = meta.fat_sec + (meta.fats * meta.sec_per_fat);
    meta.root_sectors = (meta.dir_entries * DIR_ENTRY_SIZE) / meta.sector_size;
    meta.data_sec = meta.root_sec + meta.root_sectors;
    meta.clusters = (meta.sectors - meta.data_sec) / meta.sec_per_clus;
    meta.cluster_size = meta.sec_per_clus * meta.sector_size;

    meta.fs_uid = getuid();
    meta.fs_gid = getgid();

    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    meta.atime = meta.mtime = meta.ctime = now;
    return NULL;
}

/**
 * @brief Release file system. DO NOT MODIFY!
 * 
 * @param data 
 */
void fat16_destroy(void *data) { }

/**
 * @brief Fetch the file attributes corresponding to `path`. DO NOT MODIFY!
 * 
 * @param path  : Path of the file
 * @param stbuf : Output parameter to store the attribute structure.
 * @return <int>: Return 0 on success; Return the negative value of the POSIX return code on failure.
 */
int fat16_getattr(const char* path, struct stat* stbuf, struct fuse_file_info* fi) {
    printf("getattr(path='%s')\n", path);
    // Clear all attributes
    memset(stbuf, 0, sizeof(struct stat));

    // These attributes are ignored.
    stbuf->st_dev = 0;
    stbuf->st_ino = 0;
    stbuf->st_nlink = 0;
    stbuf->st_rdev = 0;

    // These attributes are pre-calculated and will not change.
    stbuf->st_uid = meta.fs_uid;
    stbuf->st_gid = meta.fs_gid;
    stbuf->st_blksize = meta.cluster_size;

    // These attributes need to be set based on the file
    // st_mode, st_size, st_blocks, a/m/ctim
    if (path_is_root(path)) {
        stbuf->st_mode = S_IFDIR | S_NORMAL;
        stbuf->st_size = 0;
        stbuf->st_blocks = 0;
        stbuf->st_atim = meta.atime;
        stbuf->st_mtim = meta.mtime;
        stbuf->st_ctim = meta.ctime;
        return 0;
    }

    DirEntrySlot slot;
    DIR_ENTRY* dir = &(slot.dir);
    int ret = find_entry(path, &slot);
    if(ret < 0) {
        return ret;
    }
    stbuf->st_mode = get_mode_from_attr(dir->DIR_Attr);
    stbuf->st_size = dir->DIR_FileSize;
    stbuf->st_blocks = dir->DIR_FileSize / PHYSICAL_SECTOR_SIZE;
    
    time_fat_to_unix(&stbuf->st_atim, dir->DIR_LstAccDate, 0, 0);
    time_fat_to_unix(&stbuf->st_mtim, dir->DIR_WrtDate, dir->DIR_WrtTime, 0);
    time_fat_to_unix(&stbuf->st_ctim, dir->DIR_CrtDate, dir->DIR_CrtTime, dir->DIR_CrtTimeTenth);
    return 0;
}

/**
 * @brief Read directory entries from sectors starting at `first_sec` for 
 *        `nsec` number of sectors and populate them into buffer `buf` using
 *        the `filler()` function.
 *        For example, `filler(buffer, "file1.txt", NULL, 0, 0)`.
 * 
 * @param first_sec : The starting sector number
 * @param nsec      : Number of sectors
 * @param filler    : The Function used to populate results
 * @param buf       : Buffer to store the results
 * @return <int>    : Return 0 on success; -ENOERROR on error.
 */
int fill_entries_in_sectors(sector_t first_sec, size_t nsec, fuse_fill_dir_t filler, void* buf) {
    char sector_buffer[MAX_LOGICAL_SECTOR_SIZE];
    char name[MAX_NAME_LEN];
    for(size_t i=0; i < nsec; i++) {
        /**
         * TASK 1.2
         * TODO:
         *   Read directory entries from sectors [~3 lines of code]
         * 
         * Hint:
         *   To make life easier for you, most of the functionalities have
         *   already been implemented. (You only need to replace `_placeholder_`
         *   with the correct code in the lines marked with *TODO*).
         * 
         *   Please complete this loop that reads content from each sector
         *   and then iterates over directory entries to correctly all the
         *   `filler()` function. You might consider the following steps:
         * 
         *   1. Use `sector_read()` to load content of the i-th sector into
         *      `sector_buffer`. (What is the sector number?)
         *   2. Iterate over each directory entry in `sector_buffer`. If valid,
         *      convert the filename and call `filler()`.
         *      Useful functions: (Read the function prototype for yourself)
         *      - `de_is_valid()`
         *      - `to_longname()`
         *      - `filler()`
         *   3. If an empty directory entry is encountered, it means that the
         *      directory entries within that sector are already processed, 
         *      thus no need to read more entries in this sector (Why?)
         *      Useful functions: `de_is_free()`
         */

        //Modified 1
        sector_t sec = first_sec + i; // TODO: Fill in the correct sector number
        int ret = sector_read(sec, sector_buffer);
        if(ret < 0) {
            return -EIO;
        }
        //Modified 1
        for(size_t off = 0; off < MAX_LOGICAL_SECTOR_SIZE ; off += DIR_ENTRY_SIZE ) { // TODO: Fill in the loop condition. (How big is each sector? How big is each directory entry?)
            DIR_ENTRY* entry = (DIR_ENTRY*)(sector_buffer + off);
            
            if(de_is_valid(entry)) {
                //Modified 1
                int ret = to_longname(entry->DIR_Name,name,MAX_NAME_LEN ); // TODO: Please call `to_longname()` to convert `entry->DIR_Name` to a long filename and stores the result in `name`.
                if(ret < 0) {
                    return ret;
                }
                filler(buf, name, NULL, 0, 0);
            }
            if(de_is_free(entry)) {
                return 0;
            }
        }
    }
    return 0;
}

/**
 * @brief Read the directory specified by `path`, and populate to `buffer` using `filler()`
 * 
 * @param path   : The Path of the directory to read
 * @param buf    : The buffer to store the result
 * @param filler : Function used to fill results. `filler(buffer, filename, NULL, 0, 0)`
 * @param offset : Ignored
 * @param fi     : Ignored
 * @return <int> : Return 0 on success, -ENOERROR on error.
 */
int fat16_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, 
                    struct fuse_file_info *fi, enum fuse_readdir_flags flags) {
    printf("readdir(path='%s')\n", path);

    if(path_is_root(path)) {
        /**
         * TASK 1.1
         * TODO
         *   Read the root directory area [~2 lines of code]
         * 
         * Hint:
         *   Please modify the following two lines (marked with TODO), remove
         *   `_placeholder_()` and replace it with the correct code (use the 
         *   member from the global variable `meta`).
         *
         *   Note that `_placeholder_()` will appear frequently in later TODOs,
         *   it is just a place holder, like a blank that you need to fill. You
         *   should replace it with the correct code.
         */
        // Modified 1
        sector_t first_sec = meta.root_sec; // TODO: Fill in the correct *starting sector number* for the root directory area. You can refer to the definition of `meta`.
        size_t nsec = meta.root_sectors; // TODO: Fill in the correct *number of sectors* for the root directory area.
        fill_entries_in_sectors(first_sec, nsec, filler, buf);
        return 0;
    }

    // Handling non-root directories, need to find the first `clus`
    cluster_t clus = CLUSTER_END;
    DirEntrySlot slot;
    int ret = find_entry(path, &slot);
    if(ret < 0) {
        return ret;
    }

    DIR_ENTRY* dir = &(slot.dir);
    clus = dir->DIR_FstClusLO;    // Not root directory
    if(!attr_is_directory(dir->DIR_Attr)) {
        return -ENOTDIR;
    }

    char sector_buffer[MAX_LOGICAL_SECTOR_SIZE];
    char name[MAX_NAME_LEN];
    while (is_cluster_inuse(clus)) {
        sector_t first_sec = cluster_first_sector(clus);
        size_t nsec = meta.sec_per_clus;
        fill_entries_in_sectors(first_sec, nsec, filler, buf);

        clus = read_fat_entry(clus);
    }
    
    return 0;
}

/**
 * @brief Read `size` bytes of data starting at `offset` from the cluster 
 *        identified by `clus`, and writes into `data`.
 * 
 * @param clus   : Cluster number
 * @param offset : Starting offset to read
 * @param data   : Output buffer
 * @param size   : Length of data to read
 * @return <int> : Return the actual length of data read on success.
 */
int read_from_cluster_at_offset(cluster_t clus, off_t offset, char* data, size_t size) {
    assert(offset + size <= meta.cluster_size);  // offset + size should not exceed the cluster size
    char sector_buffer[PHYSICAL_SECTOR_SIZE];
    /**
     * TASK 2.2
     * TODO:
     *   Read data from a cluster [~5 lines of code]
     * 
     * Hint:
     *   1. Calculate the sector number and offset within the sector that 
     *      corresponds to the `offset`. You can use `cluster_first_sector()`
     *      to find the first sector of the cluster.
     *      However, since `offset` might exceed the size of one sector, you
     *      might need to calculate the actual sector number and the offset
     *      within that sector.
     *   2. Read the sector (using `sector_read()`)
     *   3. Move the correct portion of content from the sector to `data` buffer
     * 
     *   You only need to complete the following TODO parts. The majority of
     *   work should be in calculating the sector number and offset.
     *   Use `memcpy()` to copy to `data`.
     */
    uint32_t sec = cluster_first_sector(clus) + offset / meta.sector_size;   // TODO: Fill in the correct sector number
    size_t sec_off = offset % meta.sector_size; // TODO: Fill in the correct offset within the sector.
    size_t pos = 0;                   // The actual number of bytes already read
    while(pos < size) {               // Not yet read completely
        int ret = sector_read(sec, sector_buffer);
        if(ret < 0) {
            return ret;
        }
        /**
         * Hint: Use `memcpy()` to move data, starting from `sec_off`.
         * Questions worth asking:
         * - Move to which location within `data`?
         * - How many bytes to move?
         * - After moving, don't forget to update `pos`
         * 
         * [~3 lines of code]
        */ 
        // ================== Your code here =================
        size_t byte_read = meta.sector_size - sec_off < size - pos?
                meta.sector_size - sec_off :size - pos;

        memcpy(data + pos,sector_buffer + sec_off,byte_read);
        
        pos += byte_read;
        // ===================================================
        sec_off = 0;
        sec ++ ;
    }
    return size;
}

/**
 * @brief Read `size` bytes of data starting from `offset` bytes into the file
 *        specified by `path`, and write it into `buffer`. Return the actual
 *        number of bytes read.
 *        Hint: File size attribute is `Dir.DIR_FileSize`.
 * 
 * @param path   : Path of file to read
 * @param buffer : Result buffer
 * @param size   : Length of data to read
 * @param offset : Offset within the file where the data read starts
 * @param fi     : Ignored
 * @return <int> : Return the actual number of bytes read on success, or 0 on failure.
 */
int fat16_read(const char *path, char *buffer, size_t size, off_t offset,
               struct fuse_file_info *fi) {
    printf("read(path='%s', offset=%ld, size=%lu)\n", path, offset, size);
    if(path_is_root(path)) {
        return -EISDIR;
    }

    DirEntrySlot slot;
    DIR_ENTRY* dir = &(slot.dir);
    int ret = find_entry(path, &slot);  // Find the directory entry corresponding to the file
    if(ret < 0) {                       // Error in finding the directory entry
        return ret;
    }
    if(attr_is_directory(dir->DIR_Attr)) { // A directory found (instead of a file)
        return -EISDIR;
    }
    if(offset > dir->DIR_FileSize) {    // Offset to read from exceeds the file size
        return -EINVAL;
    }
    size = min(size, dir->DIR_FileSize - offset);  // The length of data to read cannot exceed the file size


    if(offset + size <= meta.cluster_size) {    // Case where the file is within one cluster
        cluster_t clus = dir->DIR_FstClusLO;
        int ret = read_from_cluster_at_offset(clus, offset, buffer, size);
        return ret;
    }

    // Reading a file that spans multiple clusters
    cluster_t clus = dir->DIR_FstClusLO;
    size_t p = 0;                       // Actual number of bytes read
    while(offset >= meta.cluster_size) {// Move to the correct cluster number
        if(!is_cluster_inuse(clus)) {
            return -EUCLEAN;
        }
        offset -= meta.cluster_size;    // If offset is larger than the size of a cluster, move to the next cluster, reduce offset by the size of one cluster
        clus = read_fat_entry(clus);    // Read the FAT entry
    }

    /**
     * TASK 4.2
     * TODO:
     *   Read data from multiple clusters [~10 lines of code]
     * Hint:
     *   You will need to write a loop to read the correct data from the
     *   current cluster:
     *   1. Write a while loop to confirm that there is more data to read,
     *      and the cluster number `clus` is valid.
     *      You can use `is_cluster_inuse()`
     *   2. Calculate the length of data you need to read (starting from
     *      `offset`). There are two scenarios:
     *      1. Read to the end of the cluster. What is the length?
     *      2. The remaining data does not need to read the end of the
     *         cluster. What is the length?
     *   3. Use the `read_from_cluster_at_offset()` function to read data.
     *      Be careful to check the return value.
     *   4. Update `p` (number of bytes read), `offset` (offset for the next
     *      cluster), and `clus` (next cluster number).
     *      Actually, `offset` will definitely be `0`, because apart from 
     *      the first cluster, all subsequent clusters are read from the 
     *      beginning. `clus` needs to be updated by reading the FAT table, 
     *      don't forget you've already implemented `read_fat_entry()`.
     */
    // ================== Your code here =================
    
     while (p < size && is_cluster_inuse(clus)) {
        size_t read_size = min(size - p, meta.cluster_size - offset);
        int ret = read_from_cluster_at_offset(clus, offset, buffer + p, read_size);
        if (ret < 0) {
            return ret;
        }
        p += ret;
        offset = 0; // Subsequent clusters start reading from the beginning
        clus = read_fat_entry(clus); // Move to the next cluster
    }
    
    // ===================================================
    return p;
}

int dir_entry_write(DirEntrySlot slot) {
    /**
     * TASK 3.2
     * TODO:
     *   Write a directory entry [~3 lines of code]
     * Hint:
     *   You will only need to complete the lines marked with TODO. Use
     *   `sector_write()` to write the directory entry into the file system.
     * 
     *   `sector_write()` can only write one sector, but a directory entry 
     *   occupies only a part of a sector. To avoid overwriting other directory
     *   entries, you need to read the entire sector first (using `sector_read()`),
     *   modify the appropriate position for the directory entry, then write the
     *   entire sector back.
     */

    char sector_buffer[MAX_LOGICAL_SECTOR_SIZE];
    //Modified 1
    int ret = sector_read(slot.sector, sector_buffer); // TODO: Use `sector_read()` to read the entire sector
    if(ret < 0) {
        return ret;
    }
    memcpy(sector_buffer + slot.offset, &(slot.dir), sizeof(DIR_ENTRY));
    ret = sector_write(slot.sector, sector_buffer); // TODO: Use `sector_write()` to write back the sector.
    if(ret < 0) {
        return ret;
    }
    return 0;
}

int dir_entry_create(DirEntrySlot slot, const char *shortname, 
            attr_t attr, cluster_t first_clus, size_t file_size) {
    DIR_ENTRY* dir = &(slot.dir);
    memset(dir, 0, sizeof(DIR_ENTRY));
    
    /**
     * TASK 3.1
     * TODO:
     *   Create a directory entry [~5 lines of code]
     * Hint:
     *   Please set the correct values for `DIR_Name`, `Dir_Attr`,
     *   `DIR_FstClusHI`, `DIR_FstClusLO`, `DIR_FileSize` of the 
     *   directory entry.
     * 
     *   You can use `memcpy()` to update `DIR_Name`.
     *   Note that `DIR_FstClusHI` is always set to `0` here.
     */

    // ================== Your code here =================
    //Modified 1
    
    memcpy(dir->DIR_Name, shortname, 11);
    
    dir->DIR_Attr = attr;
   
    dir->DIR_FstClusHI = 0;
    dir->DIR_FstClusLO = first_clus & 0xFFFF;
    
    dir->DIR_FileSize = file_size;
    
    
    // ===================================================
    
    // Set file timestamps. DO NOT MODIFY.
    struct timespec ts;
    int ret = clock_gettime(CLOCK_REALTIME, &ts);
    if(ret < 0) {
        return -errno;
    }
    time_unix_to_fat(&ts, &(dir->DIR_CrtDate), &(dir->DIR_CrtTime), &(dir->DIR_CrtTimeTenth));
    time_unix_to_fat(&ts, &(dir->DIR_WrtDate), &(dir->DIR_WrtTime), NULL);
    time_unix_to_fat(&ts, &(dir->DIR_LstAccDate), NULL, NULL);

    ret = dir_entry_write(slot);    // Implement this function.
    if(ret < 0) {
        return ret;
    }
    return 0;
}


/**
 * @brief Create a new file at the specified `path` (creating a file actually
 *        involves creating a directory entry)
 * 
 * @param path   : Path where the file is to be created
 * @param mode   : File type to be created, can be ignored here (regular files by default).
 * @param devNum : Ignored. Device number of the device where the file is to be created.
 * @return <int> : Return 0 on success, -ENOERROR on failure.
 */
int fat16_mknod(const char *path, mode_t mode, dev_t dev) {
    printf("mknod(path='%s', mode=%03o, dev=%lu)\n", path, mode, dev);
    DirEntrySlot slot;
    const char* filename = NULL;
    int ret = find_empty_slot(path, &slot, &filename);  // Find an empty directory entry
    if(ret < 0) {
        return ret;
    }

    char shortname[11];
    ret = to_shortname(filename, MAX_NAME_LEN, shortname); // Convert long filename to short filename
    if(ret < 0) {
        return ret;
    }
    ret = dir_entry_create(slot, shortname, ATTR_REGULAR, 0, 0); // Create directory entry
    if(ret < 0) {
        return ret;
    }
    return 0;
}

/**
 * @brief Write `data` into the FAT table entry corresponding to the cluster
 *        number `clus`. Note that the same update must be made across all
 *        FAT tables in the file system.
 * 
 * @param clus  : Cluster number of the table entry to be written
 * @param data  : Data to be written to the table entry, such as the next cluster number, `CLUSTER_END` (end of file), or 0 (free the cluster), etc.
 * @return <int>: Return 0 on success.
 */
int write_fat_entry(cluster_t clus, cluster_t data) {
    char sector_buffer[MAX_LOGICAL_SECTOR_SIZE];
    size_t clus_off = clus * sizeof(cluster_t);
    sector_t clus_sec = clus_off / meta.sector_size;
    size_t sec_off = clus_off % meta.sector_size;
    for(size_t i = 0; i < meta.fats; i++) {
        /**
         * TASK 6.2
         * TODO:
         *   Modify FAT table entry [~10 lines of code, ~4 core lines of code]
         * Hint:
         *   Modify the table entry at `sec_off` offset in `clus_sec` sector
         *   of the i-th FAT table, so that its value becomes `data`.
         *   1. Calculate the sector of the i-th FAT table where `clus` belongs,
         *      further calculate the sector where the FAT table entry for `clus`
         *      is located.
         *   2. Read that sector and modify the data at the corresponding position.
         *   3. Write back the sector.
         */
        // ================== Your code here =================
        //Modified 1
        // 1. Calculate the sector of the i-th FAT table where `clus` belongs
        sector_t fat_table_sec = meta.fat_sec + i * meta.sec_per_fat + clus_sec;

        // 2. Read that sector and modify the data at the corresponding position
        int ret = sector_read(fat_table_sec, sector_buffer);
        if (ret < 0) {
            return ret;
        }

        // Modify the table entry at `sec_off` offset
        //memcpy(sector_buffer + sec_off, &data, sizeof(cluster_t));
        *((cluster_t*)&sector_buffer[sec_off]) = data;

        // 3. Write back the sector
        ret = sector_write(fat_table_sec, sector_buffer);
        if (ret < 0) {
            return ret;
        }
        
        // ===================================================
    }
    return 0;
}

int free_clusters(cluster_t clus) {
    while(is_cluster_inuse(clus)) {
        cluster_t next = read_fat_entry(clus);
        int ret = write_fat_entry(clus, CLUSTER_FREE);
        if(ret < 0) {
            return ret;
        }
        clus = next;
    }
    return 0;
}


static const char ZERO_SECTOR[PHYSICAL_SECTOR_SIZE] = {0};
int cluster_clear(cluster_t clus) {
    sector_t first_sec = cluster_first_sector(clus);
    for(size_t i = 0; i < meta.sec_per_clus; i++) {
        sector_t sec = first_sec + i;
        int ret = sector_write(sec, ZERO_SECTOR);
        if(ret < 0) {
            return ret;
        }
    }
    return 0;
}

/**
 * @brief Allocate a free cluster and saves the cluster number in `clus`
 * 
 * @param clus  : Output parameter, used to save the allocated cluster number
 * @return <int>: Return 0 on success, -ENOERROR on failure.
 */
int alloc_one_cluster(cluster_t* clus) {
    /**
     * TASK 6.3
     * TODO:
     *   Allocate a free cluster [~15 lines of code]
     * Hint:
     *   1. Scan the FAT table, find a free cluster.
     *   2. If no free cluster is found, allocation fails, return `-ENOSPC`
     *   3. Modify the corresponding FAT table entry for the free cluster
     *      to point to `CLUSTER_END`. Also clear the cluster (use the 
     *      `cluster_clear()` function).
     *   4. Set `first_clus` to the cluster number of the first cluster,
     *      release clusters.(?)
     */
    
    // ================== Your code here =================
    //Modified 1
     char sector_buffer[MAX_LOGICAL_SECTOR_SIZE];
    
    for (cluster_t i = 2; i < meta.clusters; i++) { // FAT16保留前两个簇（0和1）
        cluster_t entry;
        size_t clus_off = i * sizeof(cluster_t); // Notice here
        sector_t clus_sec = clus_off / meta.sector_size;
        size_t sec_off = clus_off % meta.sector_size;

        // 读取FAT表项
        sector_t fat_table_sec = meta.fat_sec + clus_sec;
        int ret = sector_read(fat_table_sec, sector_buffer);
        if (ret < 0) {
            return ret;
        }

        //Modified 1
        entry = *((cluster_t*)&sector_buffer[sec_off]);

        

        // 如果找到了空闲簇
        if (entry == CLUSTER_FREE) {
            *clus = i;

            // 修改FAT表项指向CLUSTER_END
            entry = CLUSTER_END;
            //memcpy(sector_buffer + sec_off, &entry, sizeof(cluster_t));
            *((cluster_t*)&sector_buffer[sec_off]) = entry;

            // 写回FAT表项
            ret = sector_write(fat_table_sec, sector_buffer);
            if (ret < 0) {
                return ret;
            }

            
            // 清空簇
            ret = cluster_clear(*clus);
            if (ret < 0) {
                return ret;
            }

            return 0;
        }
    }
    
    // ===================================================
    return -ENOSPC;    // TODO: Delete this line or modify to the correct return value.
}

/**
 * @brief Allocate `n` free clusters. During the allocation process, `n`
 *        clusters are grouped together through FAT table entries, then
 *        return the cluster number of the first cluster.
 *        The FAT table entry of the last cluster will point to `0xFFFF`, i.e.,
 *        end of file.
 * @param n          : Number of clusters to allocate
 * @param first_clus : Output parameter, used to save the cluster number of the first cluster
 * @return <int>     : Return 0 on success, -ENOERROR on failure.
 */
int alloc_clusters(size_t n, cluster_t* first_clus) {
    if (n == 0)
        return CLUSTER_END;

    // To save the `n` free clusters, also include `CLUSTER_END` at the end, in total `n+1` clusters.
    cluster_t *clusters = malloc((n + 1) * sizeof(cluster_t));
    size_t allocated = 0;    // Number of free clusters already found.

    /**
     * TASK 8.3
     * TODO:
     *   Allocate `n` free clusters
     * Hint:
     *   1. Scan the FAT table, find `n` free clusters, store them in the
     *      `clusters` array. Note that, at this point, you do not need to
     *      modify the corresponding FAT table entries.
     *   2. If `n` clusters are not found, allocation fails, remember to 
     *      `free(clusters)`, and then return `-ENOSPC`.
     *   3. Modify the FAT table entries stored in `clusters` to connect 
     *      each cluster to the next one. Also, clear each newly allocated
     *      cluster. Remember to connect the last cluster to `CLUSTER_END`.
     *   4. Set `first_clus` to the cluster number of the first cluster, 
     *      release `clusters`.
     */


    // ================== Your code here =================
      char sector_buffer[MAX_LOGICAL_SECTOR_SIZE];
    for (cluster_t i = 2; i < meta.clusters; i++) { // FAT16保留前两个簇（0和1）
        cluster_t entry;
        size_t clus_off = i * sizeof(cluster_t);
        sector_t clus_sec = clus_off / meta.sector_size;
        size_t sec_off = clus_off % meta.sector_size;

        // 读取FAT表项
        sector_t fat_table_sec = meta.fat_sec + clus_sec;
        int ret = sector_read(fat_table_sec, sector_buffer);
        if (ret < 0) {
            free(clusters);
            return ret;
        }

        entry = *((cluster_t*)&sector_buffer[sec_off]);

        // 如果找到了空闲簇
        if (entry == CLUSTER_FREE) {
            clusters[allocated++] = i;
            if (allocated == n) {
                break;
            }
        }
    }

    if (allocated < n) {
        free(clusters);
        return -ENOSPC;
    }

    // Modify the FAT table entries to connect each cluster to the next one.
    for (size_t i = 0; i < n; i++) {
        cluster_t clus = clusters[i];
        cluster_t next_clus = (i == n - 1) ? CLUSTER_END : clusters[i + 1];

        size_t clus_off = clus * sizeof(cluster_t);
        sector_t clus_sec = clus_off / meta.sector_size;
        size_t sec_off = clus_off % meta.sector_size;

        sector_t fat_table_sec = meta.fat_sec + clus_sec;
        int ret = sector_read(fat_table_sec, sector_buffer);
        if (ret < 0) {
            free(clusters);
            return ret;
        }

        *((cluster_t*)&sector_buffer[sec_off]) = next_clus;
        ret = sector_write(fat_table_sec, sector_buffer);
        if (ret < 0) {
            free(clusters);
            return ret;
        }

        // Clear the cluster
        ret = cluster_clear(clus);
        if (ret < 0) {
            free(clusters);
            return ret;
        }
    }

    *first_clus = clusters[0];
    // ===================================================

    free(clusters);
    return 0;
}


/**
 * @brief Create a directory at the specified `path`
 * 
 * @param path   : Path where the directory is to be created
 * @param mode   : Directory mode, can be ignored here. (All directories can be assumed to be regular directories by default.)
 * @return <int> : Return 0 on success, -ENOERROR on failure.
 */
int fat16_mkdir(const char *path, mode_t mode) {
    printf("mkdir(path='%s', mode=%03o)\n", path, mode);
    DirEntrySlot slot = {{}, 0, 0};
    const char* filename = NULL;
    cluster_t dir_clus = 0; // Cluster number of the newly created directory
    int ret = 0;

    /**
     * TASK 6.1
     * TODO:
     *   Create a directory [~15 lines of code, ~4 core lines of code]
     * Hint:
     *   Similar to `mknod()`. The difference is, that you should allocate
     *   a free cluster and then create `.` and `..` directory entries in
     *   this cluster.
     * 
     *   Please call `alloc_one_cluster(&dir_clus)` function to allocate a
     *   cluster. Then implement the `alloc_one_cluster()` function.
     */

    // ================== Your code here =================
    //Modified 1
    // Find the parent directory
    ret = find_empty_slot(path, &slot, &filename);  // Find an empty directory entry
    if(ret < 0) {
        return ret;
    }

    char shortname[11];
    ret = to_shortname(filename, MAX_NAME_LEN, shortname); // Convert long filename to short filename
    if(ret < 0) {
        return ret;
    }

    // Allocate a cluster for the new directory
    ret = alloc_one_cluster(&dir_clus);
    if (ret < 0) {
        return ret;
    }
    
    // Create a new directory entry in the parent directory
    ret = dir_entry_create(slot, shortname, ATTR_DIRECTORY, dir_clus, 0);
    if (ret < 0) {
        return ret;
    }

    
    // ===================================================

    // Set `.` and `..` directory entries
    const char DOT_NAME[] =    ".          ";
    const char DOTDOT_NAME[] = "..         ";
    sector_t sec = cluster_first_sector(dir_clus);
    DirEntrySlot dot_slot = {.sector=sec, .offset=0};
    ret = dir_entry_create(dot_slot, DOT_NAME, ATTR_DIRECTORY, dir_clus, 0);
    if(ret < 0) {
        return ret;
    }
    DirEntrySlot dotdot_slot = {.sector=sec, .offset=DIR_ENTRY_SIZE};
    ret = dir_entry_create(dotdot_slot, DOTDOT_NAME, ATTR_DIRECTORY, sector_cluster(slot.sector), 0);
    if(ret < 0) {
        return ret;
    }
    return 0;
}

/**
 * @brief Delete the file specified by `path`
 * 
 * @param path   : Path of the file to be deleted
 * @return <int> : Return 0 on success, -ENOERROR on failure.
 */
int fat16_unlink(const char *path) {
    printf("unlink(path='%s')\n", path);
    DirEntrySlot slot;
    DIR_ENTRY* dir = &(slot.dir);

    /**
     * TASK 7.1
     * TODO:
     *   Delete a file [~15 lines of code, core code about 5 lines]
     * Hint:
     *   Similar to creating a file, deleting a file actually involves deleting
     *   a directory entry:
     *   1. Find the directory entry;
     *   2. Confirm the directory entry is a file;
     *   3. Free the occupied clusters;
     *   4. Mark the directory entry as deleted;
     *   5. Write back the directory entry.
     * 
     *   Useful functions:
     *   - `find_entry`
     *   - `attr_is_directory`
     *   - `free_clusters`
     *   - `dir_entry_write`
     * 
     *   Remember to check the return value after calling functions.
     */
    // ================== Your code here =================
    //Modifie 1
     int ret = find_entry(path, &slot);
    if (ret < 0) {
        return ret;
    }

    
   
    if (attr_is_directory(dir->DIR_Attr)) {
        return -EISDIR;
    }
    
    ret = free_clusters(dir->DIR_FstClusLO);
    if (ret < 0) {
        return ret;
    }
    
    dir->DIR_Name[0] = NAME_DELETED;  // Mark as deleted
    ret = dir_entry_write(slot);
    if (ret < 0) {
        return ret;
    }

    
    
    // ===================================================
    //Modified 2
    return 0; // TODO: Please modify the return value.
}

/**
 * @brief Delete the directory specified by `path`
 * 
 * @param path   : Path of the directory to be deleted
 * @return <int> : Return 0 on success, -ENOERROR on failure.
 */
int fat16_rmdir(const char *path) {
    printf("rmdir(path='%s')\n", path);
    if(path_is_root(path)) {    // The root directory cannot be deleted
        return -EBUSY;
    }

    /**
     * TASK 7.2
     * TODO:
     *   Delete a directory [~50 lines of code]
     * Hint:
     *   Overall approach similar to `readdir()` + `unlink()`:
     *   1. The deletion process is similar to deleting a file, but make
     *      sure to check if the directory is empty. If the directory is
     *      not empty, it cannot be deleted (return -ENOTEMPTY).
     *   2. The process to check if a directory is empty is similar to 
     *      `readdir()`, but you need to check each directory for emptiness,
     *      rather than populating results.
     */

    // ================== Your code here =================
    //Modified 1
    DirEntrySlot slot;
    int ret = find_entry(path, &slot);
    if (ret < 0) {
        return ret;
    }
    
    if (!attr_is_directory(slot.dir.DIR_Attr)) {
        return -ENOTDIR;
    }
    
    cluster_t clus = slot.dir.DIR_FstClusLO;
    sector_t sec = cluster_first_sector(clus);
    size_t entries = meta.cluster_size / DIR_ENTRY_SIZE;
    
    char sector_buffer[MAX_LOGICAL_SECTOR_SIZE];
    for (size_t i = 0; i < entries; i++) {
        ret = sector_read(sec + i / (meta.sector_size / DIR_ENTRY_SIZE), sector_buffer);
        if (ret < 0) {
            return ret;
        }
        DIR_ENTRY* entry = (DIR_ENTRY*)(sector_buffer + (i % (meta.sector_size / DIR_ENTRY_SIZE)) * DIR_ENTRY_SIZE);
        if (entry->DIR_Name[0] != NAME_FREE && entry->DIR_Name[0] != NAME_DELETED) {
            if (memcmp(entry->DIR_Name, ".          ", FAT_NAME_LEN) != 0 &&
                memcmp(entry->DIR_Name, "..         ", FAT_NAME_LEN) != 0) {
                return -ENOTEMPTY;
            }
        }
    }

    ret = free_clusters(slot.dir.DIR_FstClusLO);
    if (ret < 0) {
        return ret;
    }
    
    slot.dir.DIR_Name[0] = NAME_DELETED;  // Mark as deleted
    ret = dir_entry_write(slot);
    if (ret < 0) {
        return ret;
    }

    
    
    // ===================================================
    return 0; // TODO: Please modify the return value.
}

/**
 * @brief Modify the timestamps of the file specified by `path`. This function
 *        is not required for implementation, can be ignored.
 * 
 * @param path   : Path of the file whose timestamps are to be modified
 * @param tv     : Timestamps
 * @return <int> 
 */
int fat16_utimens(const char *path, const struct timespec tv[2], struct fuse_file_info* fi) {
    printf("utimens(path='%s', tv=[%ld.%09ld, %ld.%09ld])\n", path, 
                tv[0].tv_sec, tv[0].tv_nsec, tv[1].tv_sec, tv[1].tv_nsec);
    DirEntrySlot slot;
    DIR_ENTRY* dir = &(slot.dir);
    int ret = find_entry(path, &slot);
    if(ret < 0) {
        return ret;
    }

    time_unix_to_fat(&tv[1], &(dir->DIR_WrtDate), &(dir->DIR_WrtTime), NULL);
    time_unix_to_fat(&tv[0], &(dir->DIR_LstAccDate), NULL, NULL);
    ret = dir_entry_write(slot);
    if(ret < 0) {
        return ret;
    }
    
    return 0;
}

/**
 * @brief Write data from `data` into cluster identified by `clus` starting
 *        at `offset`.
 *        Note that size+offset <= cluster size
 * 
 * @param clus      : Cluster number where the data is to be written
 * @param data      : Data to be written
 * @param size      : Size of data to be written (in bytes)
 * @param offset    : Offset within the cluster where the data writing starts
 * @return <ssize_t>: Return number of bytes written on success, -ENOERROR on failure.
 */
ssize_t write_to_cluster_at_offset(cluster_t clus, off_t offset, const char* data, size_t size) {
    assert(offset + size <= meta.cluster_size);  // offset + size must not exceed cluster size
    char sector_buffer[PHYSICAL_SECTOR_SIZE];

    /**
     * TASK 8.2
     * TODO:
     *   Write data into a cluster [~20 lines of code]
     * Hint:
     *   Refer to `read_from_cluster_at_offset`.
     *   Basically, each sector requires a read -> modify -> write process.
     */

    // ================== Your code here =================
    size_t sector_size = meta.sector_size;
    size_t cluster_size = meta.cluster_size;
    sector_t first_sector = cluster_first_sector(clus);
    ssize_t total_written = 0;

    while (size > 0) {
        sector_t sector = first_sector + (offset / sector_size);
        size_t sector_offset = offset % sector_size;
        size_t to_write = min(size, sector_size - sector_offset);

        int ret = sector_read(sector, sector_buffer);
        if (ret < 0) {
            return ret;
        }

        memcpy(sector_buffer + sector_offset, data, to_write);
        ret = sector_write(sector, sector_buffer);
        if (ret < 0) {
            return ret;
        }

        data += to_write;
        size -= to_write;
        offset += to_write;
        total_written += to_write;
    }
    
    
    // ===================================================
    return total_written; // TODO: Please modify the return value.
}

/**
 * @brief Write `size` bytes of data from `data` to the file specified by `path`
 *        starting at `offset`. Note that when the amount of data written 
 *        exceeds the file's own size, the file's size needs to be expanded,
 *        and if necessary, new clusters need to be allocated.
 * 
 * @param path    : Path of the file where data is to be written
 * @param data    : Data to be written
 * @param size    : Length of data to be written
 * @param offset  : Offset within the file where data writing starts (in bytes)
 * @param fi      : This parameter can be ignored here.
 * @return <int>  : Return number of bytes written on success, -ENOERROR on failure.
 */
int fat16_write(const char *path, const char *data, size_t size, off_t offset,
                struct fuse_file_info *fi) {
    printf("write(path='%s', offset=%ld, size=%lu)\n", path, offset, size);
    if(path_is_root(path)) {
        return -EISDIR;
    }

    DirEntrySlot slot;
    DIR_ENTRY* dir = &(slot.dir);
    int ret = find_entry(path, &slot);
    if(ret < 0) {
        return ret;
    }
    if(attr_is_directory(dir->DIR_Attr)) {
        return -EISDIR;
    }
    if(offset > dir->DIR_FileSize) {
        return -EINVAL;
    }
    if(size == 0) {
        return 0;
    }

    /**
     * TASK 8.1
     * TODO:
     *   Write data into the file, allocate new clusters if necessary
     *   [~50 lines of code]
     * Hint: None. You are on your own.
     * 
     */
    // ================== Your code here =================
     
    size_t cluster_size = meta.cluster_size;
    cluster_t current_cluster = dir->DIR_FstClusLO;
    cluster_t prev_cluster = 0;
    size_t bytes_written = 0;

    // Navigate to the right cluster
    off_t cluster_offset = offset / cluster_size;
    size_t cluster_inner_offset = offset % cluster_size;

    while (cluster_offset > 0 && current_cluster >= CLUSTER_MIN && current_cluster < CLUSTER_END_BOUND) {
        prev_cluster = current_cluster;
        ret = read_fat_entry(current_cluster);
        if (ret < 0) {
            return ret;
        }
        cluster_offset--;
    }

    // Write data to clusters
    while (size > 0) {
        if (current_cluster < CLUSTER_MIN || current_cluster >= CLUSTER_END_BOUND) {
            // Allocate new clusters if necessary
            size_t clusters_needed = (size + cluster_size - 1) / cluster_size;
            cluster_t first_new_cluster;
            ret = alloc_clusters(clusters_needed, &first_new_cluster);
            if (ret < 0) {
                return ret;
            }

            if (prev_cluster) {
                ret = write_fat_entry(prev_cluster, first_new_cluster);
                if (ret < 0) {
                    return ret;
                }
            } else {
                dir->DIR_FstClusLO = first_new_cluster;
            }
            current_cluster = first_new_cluster;
        }

        size_t to_write = min(size, cluster_size - cluster_inner_offset);
        ssize_t written = write_to_cluster_at_offset(current_cluster, cluster_inner_offset, data, to_write);
        if (written < 0) {
            return written;
        }

        data += written;
        size -= written;
        offset += written;
        bytes_written += written;
        cluster_inner_offset = 0;

        // Move to the next cluster if necessary
        if (size > 0) {
            prev_cluster = current_cluster;
            ret = read_fat_entry(current_cluster);
            if (ret < 0) {
                return ret;
            }
        }
    }

    // Update the directory entry file size if needed
    if (offset > dir->DIR_FileSize) {
        dir->DIR_FileSize = offset;
        ret = dir_entry_write(slot);
        if (ret < 0) {
            return ret;
        }
    }

    return bytes_written;
}

/**
 * @brief Change the size of the file specified by `path` to `size`. Note that
 *        `size` can be larger, smaller, or equal to the original file size.
 *        - If `size` is larger than the original file size, the expanded part
 *          must be set to zero, and if necessary, new clusters must be allocated.
 *        - If `size` is smaller than the original file size, the file will be
 *          truncated from the end, and if any clusters are no longer used, they
 *          should be freed.
 *        - If `size` is equal to the original file size, nothing needs to be done.
 * 
 * @param path   : Path of the file whose size is to be changed
 * @param size   : New file size
 * @return <int> : Return 0 on success, -ENOERROR on failure.
 */
int fat16_truncate(const char *path, off_t size, struct fuse_file_info* fi) {
    printf("truncate(path='%s', size=%lu)\n", path, size);
    if(path_is_root(path)) {
        return -EISDIR;
    }

    DirEntrySlot slot;
    DIR_ENTRY* dir = &(slot.dir);
    int ret = find_entry(path, &slot);
    if(ret < 0) {
        return ret;
    }
    if(attr_is_directory(dir->DIR_Attr)) {
        return -EISDIR;
    }

    size_t old_size = dir->DIR_FileSize;
    if(old_size == size) {
        return 0;
    } else if(size > old_size) {
        size_t need_clus = (size + meta.cluster_size - 1) / meta.cluster_size;
        cluster_t clus = dir->DIR_FstClusLO;
        cluster_t last_clus = 0;
        while(is_cluster_inuse(clus)) {
            last_clus = clus;
            need_clus --;
            clus = read_fat_entry(clus);
        }

        cluster_t new;
        int ret = alloc_clusters(need_clus, &new);
        if(ret < 0) {
            return ret;
        }
        ret = write_fat_entry(last_clus, new);

        if(last_clus == 0) {
            dir->DIR_FstClusLO = new;
        }
    } else if(size < old_size) {
        size_t need_clus = (size + meta.cluster_size - 1) / meta.cluster_size;
        cluster_t clus = dir->DIR_FstClusLO;
        cluster_t last_clus = 0;
        while(need_clus > 0) {
            need_clus --;
            last_clus = clus;
            clus = read_fat_entry(clus);
        }
        if(last_clus == 0) {
            dir->DIR_FstClusLO = CLUSTER_FREE;
        } else {
            int ret;
            ret = free_clusters(clus);
            if(ret < 0) {
                return ret;
            }
            ret = write_fat_entry(last_clus, CLUSTER_END);
            if(ret < 0) {
                return ret;
            }
        }
    }

    dir->DIR_FileSize = size;
    dir_entry_write(slot);

    return 0;
}


struct fuse_operations fat16_oper = {
    .init = fat16_init,         // File system initialization
    .destroy = fat16_destroy,   // File system termination
    .getattr = fat16_getattr,   // Get file attributes

    .readdir = fat16_readdir,   // Read directory
    .read = fat16_read,         // Read file

    .mknod = fat16_mknod,       // Create file
    .unlink = fat16_unlink,     // Delete file
    .utimens = fat16_utimens,   // Modify file timestamps

    .mkdir = fat16_mkdir,       // Create directory
    .rmdir = fat16_rmdir,       // Delete directory

    .write = fat16_write,       // Write to file
    .truncate = fat16_truncate  // Change file size
};
