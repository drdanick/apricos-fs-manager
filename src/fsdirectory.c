#include <stdlib.h>
#include <string.h>
#include "fsdirectory.h"
#include "apricosfsman.h"
#include "diskio.h"
#include "allocator.h"
#include "filesystem.h"

long long createDirectory(Filesystem* fs) {
    long long directoryBlock = findNextFreeBlock(fs, 0);

    if(directoryBlock != -1 && createDirectoryAtBlock(fs, directoryBlock)) {
        return directoryBlock;
    }

    return -1;
}

int createDirectoryAtBlock(Filesystem* fs, unsigned int blockNum) {
    char* dirMetadataSector;
    if(!allocateBlock(fs, blockNum))
        return 0;

    dirMetadataSector = getBlockData(fs->diskData, blockNum);
    memset(dirMetadataSector, '\0', SECTOR_SIZE);

    return 1;
}

int pushDirectoryToStack(Filesystem* fs, FsDirectory dir) {
    if(fs->currentPathUnit < MAX_PATH_DEPTH) {
        fs->pathStack[fs->currentPathUnit] = dir;
        fs->currentPathUnit++;

        return 1;
    }

    return -1;
}

FsDirectory popDirectoryFromStack(Filesystem* fs) {
    FsDirectory directory;

    if(fs->currentPathUnit <= 0) {
        directory.name = NULL;
        directory.rawData = NULL;
        directory.dirEntries = NULL;
    } else {
        directory = fs->pathStack[--fs->currentPathUnit];
    }

    return directory;
}

int openBlockAsDirectory(Filesystem* fs, unsigned int blockNum, char* dirName, FsDirectory* dir) {
    if(!dir) {
        return 0;
    }

    if(!isBlockFree(fs, blockNum)) {
        char* dirMetadataSector = getBlockData(fs->diskData, blockNum);

        dir->name = dirName;
        dir->block = blockNum;
        dir->rawData = dirMetadataSector;
        dir->dirEntries = (FsDirectoryEntry*) dirMetadataSector;

        return 1;
    }

    return 0;
}

int getFsDirectoryFromEntry(Filesystem* fs, FsDirectoryEntry* entry, FsDirectory* dir) {
    if(!dir) {
        return 0;
    }

    if(!fs || !entry || entry->markerAndTrackNum & DIR_ENTRY_TYPE_MASK) {
        return 0;
    } else {
        unsigned int track = entry->markerAndTrackNum & TRACK_ID_MASK;
        unsigned int sector = entry->sectorNum & SECTOR_ID_MASK;
        unsigned int block = TRACK_AND_SECTOR_TO_BLOCK(track, sector);

        return openBlockAsDirectory(fs, block, entry->name, dir);
    }
}

int isDirEntryFree(FsDirectoryEntry* entry) {
    return !(entry->markerAndTrackNum & VALID_DIR_ENTRY_MASK);
}

int findNextFreeDirEntry(FsDirectory* dir) {
    int i = 0;
    for( ; i < MAX_DIR_ENTRIES; i++) {
        if(isDirEntryFree(&dir->dirEntries[i])) {
            return i;
        }
    }

    return -1;
}

int countDirectoryEntries(FsDirectory* dir) {
    int i = 0;
    int count = 0;
    for( ; i < MAX_DIR_ENTRIES; i++) {
        if(isDirEntryFree(&dir->dirEntries[i])) {
            continue;
        }
        count++;
    }

    return count;
}

FsDirectoryEntry* findDirEntryByName(FsDirectory* dir, char* name) {
    static char nameBuff[MAX_DIR_ENTRY_NAME_LENGTH + 1];
    static char searchName[MAX_DIR_ENTRY_NAME_LENGTH + 1];
    int i = 0;
    FsDirectoryEntry* entries = dir->dirEntries;

    memset(nameBuff, '\0', MAX_DIR_ENTRY_NAME_LENGTH + 1);

    /* Names are stored cut short to MAX_DIR_ENTRY_NAME_LENGTH, so compare them that way */
    memset(searchName, '\0', sizeof(searchName));
    memcpy(searchName, name, MIN(MAX_DIR_ENTRY_NAME_LENGTH, strlen(name)));

    for( ; i < MAX_DIR_ENTRIES; i++) {
        FsDirectoryEntry* entry = &entries[i];
        if(isDirEntryFree(entry)) {
            continue;
        }

        /* convert entry name into a c string */
        memcpy(nameBuff, entry->name, MAX_DIR_ENTRY_NAME_LENGTH);
        if(strcmp(nameBuff, searchName) == 0) {
            return entry;
        }
    }

    return NULL;
}

void printDirectoryEntryAttributes(FsDirectoryEntry* entry, char* prefix, char* suffix) {
    printf("%s%c%c%s",
            strlen(prefix) > 0 ? prefix : "",
            entry->markerAndTrackNum & DIR_ENTRY_TYPE_MASK ? 'f' : 'd',
            entry->markerAndTrackNum & DIR_ENTRY_EXECUTABLE_MASK ? 'x' : '-',
            strlen(suffix) > 0 ? suffix : ""
            );
}

void printDirectoryListing(FsDirectory* dir, char* prefix) {
    char nameBuff[MAX_DIR_ENTRY_NAME_LENGTH + 1]; /* Used to convert dir names to c style strings with null terminator */
    int i = 0;

    if(!prefix)
        prefix = "";

    memset(nameBuff, '\0', MAX_DIR_ENTRY_NAME_LENGTH + 1);

    for( ; i < MAX_DIR_ENTRIES; i++) {
        char* suffix;
        FsDirectoryEntry* entry = &dir->dirEntries[i];

        if(isDirEntryFree(entry)) {
            continue;
        }

        suffix = (entry->markerAndTrackNum & DIR_ENTRY_TYPE_MASK) ? "" : "/";
        memcpy(nameBuff, entry->name, sizeof(nameBuff));
        printf("%s", prefix);
        printDirectoryEntryAttributes(entry, "[", "] ");
        printf("%s%s\n", nameBuff, suffix);
    }
}

int removeDirEntry(FsDirectoryEntry* entry) {
    if(isDirEntryFree(entry)) {
        return 0;
    }

    /* clear the first byte */
    entry->markerAndTrackNum = 0x00;

    /* clear the name */
    memset(entry->name, '\0', MAX_DIR_ENTRY_NAME_LENGTH);

    return 1;
}

int removeDirEntryByName(FsDirectory* dir, char* name) {
    FsDirectoryEntry* entry = findDirEntryByName(dir, name);

    if(entry == NULL) {
        return 0;
    }

    return removeDirEntry(entry);
}

int allocateAndAddEntryToDirectory(Filesystem* fs, FsDirectory* parentDir, char* childName, char isFile) {
    long long blockNum = createDirectory(fs);

    if(blockNum < 0)
        return 0;

    if(!addBlockEntrytoDirectory(parentDir, blockNum, childName, isFile)) {
        freeBlock(fs, blockNum);
        return 0;
    }

    return 1;
}

int addBlockEntrytoDirectory(FsDirectory* parentDir, unsigned int childBlock, char* childName, char isFile) {
    int childTrack = BLOCK_TO_TRACK(childBlock);
    int childSector = BLOCK_TO_SECTOR(childBlock);

    return addEntrytoDirectory(parentDir, childTrack, childSector, childName, isFile);
}

int addEntrytoDirectory(FsDirectory* parentDir, int childtrack, int childSector, char* childName, char isFile) {
    FsDirectoryEntry* entry;
    int childIndex = findNextFreeDirEntry(parentDir);

    entry = findDirEntryByName(parentDir, childName);

    if(entry || childIndex == -1) {
        return 0;
    }

    entry = &parentDir->dirEntries[childIndex];

    entry->markerAndTrackNum =  VALID_DIR_ENTRY_MASK | (childtrack & DIR_ENTRY_TRACK_MASK) | (isFile ? DIR_ENTRY_TYPE_MASK : 0x00);
    entry->sectorNum = childSector & DIR_ENTRY_SECTOR_MASK;

    /* Clear the name field, then copy new data over (unused characters must be null) */
    memset(entry->name, '\0', MAX_DIR_ENTRY_NAME_LENGTH);
    memcpy(entry->name, childName, MIN(MAX_DIR_ENTRY_NAME_LENGTH, strlen(childName)));

    return 1;
}

void directoryEntryToggleFlag(FsDirectoryEntry* entry, char flag) {
    entry->markerAndTrackNum ^= flag;
}
