#ifndef DISKIO_H
#define DISKIO_H

#include <stdio.h>

/*
 * Disk geometry constants
 */
#define TRACKS            32
#define SECTORS_PER_TRACK 64
#define SECTOR_SIZE       256
#define TRACK_SIZE        (SECTORS_PER_TRACK * SECTOR_SIZE)
#define SECTORS           (SECTORS_PER_TRACK * TRACKS)
#define DISK_SIZE         (TRACKS * SECTORS_PER_TRACK * SECTOR_SIZE)

/*
 * Masks to prevent IDs from being out of range
 */
#define TRACK_ID_MASK  0x1F
#define SECTOR_ID_MASK 0x3F

/*
 * Functions
 */

/**
 * Load and cache data from a disk image file.
 *
 * fileName: The name/path of the disk image file to load
 *
 * Returns: A pointer to the loaded data.
 */
char* loadDataFromDiskFile(char* fileName);

/**
 * Create a byte array the size of a disk image.
 * The data is initialized to be all zero.
 *
 * Returns: A pointer to the aloocated disk image data.
 */
char* createDiskData();

/**
 * Save disk image data to a file
 * 
 * fileName: The name/path of the file to save to.
 * data:     A pointer to the data to save.
 *
 * Returns: 1 if the operation was successful, -1 otherwise.
 *
 */
int saveDiskFile(char* fileName, char* data);

/**
 * Free cached disk data
 *
 * diskData: A pointer to the disk data cache.
 */
void freeDiskData(char* diskData);

/**
 * Given cached disk data, retrieve a pointer to a specific disk sector.
 *
 * data:      A pointer to the disk data cache.
 * tracknum:  The track number to load.
 * sectornum: The sector number to load.
 *
 * Returns: A pointer to the requested segment within the cache.
 * Note that no data is copied.
 */
char* getSectorData(char* data, int tracknum, int sectornum);

/**
 * Given cached disk data and a block number, retrieve a pointer to a
 * specific disk sector.
 *
 * data:      A pointer to the disk data cache.
 * blockNum:  The block number to load.
 *
 * Returns: A pointer to the requested segment within the cache.
 * Note that no data is copied.
 */
char* getBlockData(char* data, int blockNum);

#endif /* DISKIO_H */
