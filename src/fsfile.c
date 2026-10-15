#include <string.h>
#include <stdio.h>
#include "fsfile.h"
#include "filesystem.h"
#include "fsdirectory.h"
#include "allocator.h"
#include "apricosfsman.h"

long long createFile(Filesystem* fs, FsDirectory* parentDir, char* name) {
    long long fileMetadataBlock = findNextFreeBlock(fs, 0);

    if(fileMetadataBlock != -1 && createFileAtBlock(fs, parentDir, fileMetadataBlock, name)) {
        return fileMetadataBlock;
    }

    return -1;
}

int createFileAtBlock(Filesystem* fs, FsDirectory* parentDir, unsigned int blockNum, char* name) {
    char* fileMetadata;
    int nextFreeDirEntry = findNextFreeDirEntry(parentDir);

    if(nextFreeDirEntry == -1 || !allocateBlock(fs, blockNum)) {
        return 0;
    }

    /* Obtain and clear the file metadata sector */
    fileMetadata = getBlockData(fs->diskData, blockNum);
    memset(fileMetadata, '\0', SECTOR_SIZE);


    /* Add the block to the parent directory */
    if(!addBlockEntrytoDirectory(parentDir, blockNum, name, 1)) {
        /* since this failed, free the previously allocated block */
        freeBlock(fs, blockNum);
        return 0;
    }

    /* Initialize the size field */
    ((FsFileMetadata*)fileMetadata)->fileSize = 0;

    return 1;
}

int openBlockAsFile(Filesystem* fs, unsigned int blockNum, char* fileName, FsFile* file) {
    if(!file) {
        return 0;
    }

    if(isBlockFree(fs, blockNum)) {
        return 0;
    } else {
        memset(file->name, '\0', sizeof(file->name));
        memcpy(file->name, fileName, MIN(MAX_DIR_ENTRY_NAME_LENGTH, strlen(fileName)));
        file->rawData = getBlockData(fs->diskData, blockNum);
        file->fileMetadata = (FsFileMetadata*)file->rawData;

        return 1;
    }
}

int getFsFileFromEntry(Filesystem* fs, FsDirectoryEntry* entry, FsFile* file) {
    static char nameBuffer[sizeof(file->name)];

    if(!file) {
        return 0;
    }

    if(!fs || !entry || !(entry->markerAndTrackNum & DIR_ENTRY_TYPE_MASK)) {
        return 0;
    } else {
        unsigned int track = entry->markerAndTrackNum & TRACK_ID_MASK;
        unsigned int sector = entry->sectorNum & SECTOR_ID_MASK;
        unsigned int block = TRACK_AND_SECTOR_TO_BLOCK(track, sector);

        /* Convert entry name into a C style string */
        memset(nameBuffer, '\0', sizeof(nameBuffer));
        memcpy(nameBuffer, entry->name, MAX_DIR_ENTRY_NAME_LENGTH);

        return openBlockAsFile(fs, block, nameBuffer, file);
    }
}

int getIndexOfFileBlock(FsFile* file, FsFileBlockPointer* blockPointer) {
    int i = 0;

    for( ; i < MAX_FILE_BLOCKS; i++) {
        if(&file->fileMetadata->filePointers[i] == blockPointer) {
            return i;
        }
    }

    return -1;
}

FsFileBlockPointer* getNextFileBlock(Filesystem* fs, FsFile* file, FsFileBlockPointer* currentBlockPointer) {
    int i = -1;

    if(currentBlockPointer) {
        i = getIndexOfFileBlock(file, currentBlockPointer);

        if(i == -1) {
            return NULL;
        }
    }

    for(i += 1; i < MAX_FILE_BLOCKS; i++) {
        FsFileBlockPointer* nextBlockPointer = &file->fileMetadata->filePointers[i];
        if(nextBlockPointer->track != 0) {
            return nextBlockPointer;
        }
    }

    return NULL;
}

FsFileBlockPointer* getLastAllocatedFileBlock(Filesystem* fs, FsFile* file) {
    int count = countFileBlocks(file);

    (void)fs;

    return count ? &file->fileMetadata->filePointers[count - 1] : NULL;
}

int countFileBlocks(FsFile* file) {
    int count = 0;

    /* Blocks are used in order, and track 0 marks an unused slot */
    while(count < MAX_FILE_BLOCKS && file->fileMetadata->filePointers[count].track)
        count++;

    return count;
}

void truncateFileBlocks(Filesystem* fs, FsFile* file, int keepBlocks) {
    int i = countFileBlocks(file);

    while(i-- > keepBlocks) {
        FsFileBlockPointer* blockPointer = &file->fileMetadata->filePointers[i];

        freeSector(fs, blockPointer->track, blockPointer->sector);
        blockPointer->track = 0;
        blockPointer->sector = 0;
    }
}

int allocateNewFileData(Filesystem* fs, FsFile* file, unsigned int numBytes, FsFileAllocatedSpacePointer* outBlocks, int maxBlocks) {
    unsigned int fileSize = file->fileMetadata->fileSize;
    unsigned int usedInLastBlock = fileSize % SECTOR_SIZE;
    unsigned int residualFreeSpace = usedInLastBlock ? SECTOR_SIZE - usedInLastBlock : 0;
    unsigned int residualBytes = MIN(numBytes, residualFreeSpace);
    unsigned int newBytes = numBytes - residualBytes;
    int newBlocksRequired = (newBytes + SECTOR_SIZE - 1) / SECTOR_SIZE;
    int blocksBefore = countFileBlocks(file);
    int numChunks = 0;
    int i;

    /* If the amount of space we need cannot be allocated for the current file, return -1 */
    if(numBytes > (MAX_FILE_BLOCKS * SECTOR_SIZE) - fileSize) {
        return -1;
    }

    /* If the data needs more chunks than the caller has room for, return -1 */
    if((residualBytes ? 1 : 0) + newBlocksRequired > maxBlocks) {
        return -1;
    }

    /* Fill the unused end of the last block first */
    if(residualBytes) {
        FsFileBlockPointer* lastBlock = getLastAllocatedFileBlock(fs, file);
        if(!lastBlock) {
            return -1;
        }

        outBlocks[numChunks].dataPointer = &getSectorData(fs->diskData, lastBlock->track, lastBlock->sector)[usedInLastBlock];
        outBlocks[numChunks].size = residualBytes;
        numChunks++;
    }

    for(i = 0; i < newBlocksRequired; i++) {
        long long newBlock = allocateNewFileBlock(fs, file);

        if(newBlock == -1) {
            /* Out of space: give back the blocks allocated so far */
            truncateFileBlocks(fs, file, blocksBefore);
            return -1;
        }

        outBlocks[numChunks].dataPointer = getBlockData(fs->diskData, newBlock);
        outBlocks[numChunks].size = MIN(newBytes, SECTOR_SIZE);
        newBytes -= outBlocks[numChunks].size;
        numChunks++;
    }

    file->fileMetadata->fileSize += numBytes;
    return numChunks;
}


long long allocateNewFileBlock(Filesystem* fs, FsFile* file) {
    FsFileMetadata* metadata = file->fileMetadata;
    int i = 0;

    for( ; i < MAX_FILE_BLOCKS; i++) {
        if(!metadata->filePointers[i].track) {
            long long newBlock = autoAllocateBlock(fs);

            if(newBlock == -1) {
                return -1;
            }

            metadata->filePointers[i].track = BLOCK_TO_TRACK(newBlock);
            metadata->filePointers[i].sector = BLOCK_TO_SECTOR(newBlock);

            return newBlock;
        }
    }

    return -1;
}

int deleteAllFileBlocks(Filesystem* fs, FsFile* file) {
    FsFileMetadata* metadata = file->fileMetadata;
    int i = 0;
    int error = 0;

    for( ; i < MAX_FILE_BLOCKS; i++) {
        FsFileBlockPointer* blockPointer = &metadata->filePointers[i];
        if(!blockPointer->track) {
            continue;
        }

        if(!freeSector(fs, blockPointer->track, blockPointer->sector)) {
            error = 1;
            continue;
        }

        blockPointer->track = 0;
        blockPointer->sector = 0;
    }

    metadata->fileSize = 0;

    return !error;
}

long long appendDataFromFileStream(Filesystem* fs, FsFile* file, char* fileName) {
    static FsFileAllocatedSpacePointer newData[MAX_FILE_BLOCKS];
    static char diskBuffer[SECTOR_SIZE];
    long long totalBytesRead = 0;
    int currBytesRead;
    unsigned int sizeBefore = file->fileMetadata->fileSize;
    int blocksBefore = countFileBlocks(file);

    FILE* dataFile = fopen(fileName, "rb");
    if(!dataFile) {
        printf("cannot open %s\n",fileName);
        return -1;
    }

    while(currBytesRead = fread(diskBuffer, sizeof(char), sizeof(diskBuffer), dataFile), currBytesRead) {
        int newDataBlocks = allocateNewFileData(fs, file, currBytesRead, newData, MAX_FILE_BLOCKS);
        int bytesWritten = 0;
        int i = 0;
        totalBytesRead += currBytesRead;

        if(newDataBlocks < 1) {
            /* Leave the file as it was before the load */
            truncateFileBlocks(fs, file, blocksBefore);
            file->fileMetadata->fileSize = sizeBefore;
            fclose(dataFile);
            return -1;
        }

        /* Copy data into the given blocks */
        for( ; i < newDataBlocks; i++) {
            memcpy(newData[i].dataPointer, &diskBuffer[bytesWritten], newData[i].size);
            bytesWritten += newData[i].size;
        }
    }

    fclose(dataFile);
    return totalBytesRead;
}

long long writeFileDataToFileStream(Filesystem* fs, FsFile* file, char* fileName) {
    unsigned int remainingData = file->fileMetadata->fileSize;
    FsFileBlockPointer* nextFileBlock = NULL;

    FILE* dataFile = fopen(fileName, "wb");
    if(!dataFile) {
        printf("Cannot open %s\n", fileName);
        return -1;
    }

    while(nextFileBlock = getNextFileBlock(fs, file, nextFileBlock), nextFileBlock) {
        int dataToCopy = remainingData;
        if(remainingData > SECTOR_SIZE) {
            dataToCopy = SECTOR_SIZE;
        }

        fwrite(getSectorData(fs->diskData, nextFileBlock->track, nextFileBlock->sector), sizeof(char), dataToCopy, dataFile);
        remainingData -= dataToCopy;
    }

    fclose(dataFile);
    return file->fileMetadata->fileSize - remainingData;
}
