#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include <stdio.h>
#include "diskio.h"

/*
 * Constants
 */

#define MAX_PATH_DEPTH 64

/* disk layout constants */
/* Sectors of track 0. The spec numbers sectors from 1 and calls these
 * sector 63 (second to last) and sector 64 (last) of track 1. */
#define SPACE_BITMAP_SEGMENT 62
#define VOLUME_INFORMATION_SEGMENT 63

/* Volume info constants */
#define VOLUME_NAME_LENGTH 8
#define ROOT_FOLDER_TRACK  1
#define ROOT_FOLDER_SECTOR 0
#define BOOT_SIGNATURE_SIZE 2
#define DISK_SIGNATURE_SIZE 4

/* Directory entry masks */
#define VALID_DIR_ENTRY_MASK      0x80
#define DIR_ENTRY_EXECUTABLE_MASK 0x40
#define DIR_ENTRY_TYPE_MASK       0x20
#define DIR_ENTRY_TRACK_MASK      0x1F
#define DIR_ENTRY_SECTOR_MASK     0x3F

/* Directory entry constants */
#define MAX_DIR_ENTRY_NAME_LENGTH 6
#define MAX_DIR_ENTRIES           32

/* File Metadata constants */
#define MAX_FILE_BLOCKS 127

/* File Metadata masks*/
#define FILE_METADATA_SIZE_MASK 0x0FFFF


/*
 * Data structures
 */

/* NOTE: This maps directly to a pre-existing memory structure,
 * so care should be taken not to change the structs byte alignment. */
typedef struct {
    char markerAndTrackNum;
    char sectorNum;
    char name[MAX_DIR_ENTRY_NAME_LENGTH];
} FsDirectoryEntry;

typedef struct {
    char* name;
    unsigned int block;
    char* rawData;
    FsDirectoryEntry* dirEntries;
} FsDirectory;

typedef struct {
    char track;
    char sector;
} FsFileBlockPointer;

typedef struct {
    unsigned int fileSize: 16;
    FsFileBlockPointer filePointers[MAX_FILE_BLOCKS];
} FsFileMetadata;

/* Used to specify a sequence of free bytes within an 
 * existing block
 */
typedef struct {
    char* dataPointer;
    unsigned int size : 16;
} FsFileAllocatedSpacePointer;

typedef struct {
    char name[MAX_DIR_ENTRY_NAME_LENGTH + 1];
    char* rawData;
    FsFileMetadata* fileMetadata;
} FsFile;

typedef struct {
    char diskSignature[DISK_SIGNATURE_SIZE];
    char volumeName[VOLUME_NAME_LENGTH];
} VolumeInfo;

typedef struct {
    char* diskImagePath;
    char* spaceBitmap;
    VolumeInfo* volumeInfo;
    char* rawVolumeInfo;
    char* diskData;
    FsDirectory pathStack[MAX_PATH_DEPTH];
    int currentPathUnit; /* Always points to the next free slot */
} Filesystem;

/**
 * Constants
 */
extern const char bootSignature[];
extern const char diskSignature[];


/*
 * Filesystem control functions
 */

Filesystem* mountFilesystem(char* filePath);
void syncFilesystem(Filesystem* fs);
void unmountFilesystem(Filesystem* fs, char save);
void resetDirectoryStack(Filesystem* fs);
void clearDirectoryStack(Filesystem* fs);
FsDirectory* getWorkingDirectory(Filesystem* fs);
void formatFilesystem(Filesystem* fs, char* volumeName);

/*
 * Misc utility functions
 */
void printPathString(Filesystem* fs);
FsDirectory* savePathStack(Filesystem* fs);
void restorePathStack(Filesystem* fs, FsDirectory* srcStack, int srcStackNextFreeSlot);
void freePathStack(FsDirectory* pathStack);

#endif /* FILESYSTEM_H */
