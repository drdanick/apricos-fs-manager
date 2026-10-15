#include "fixture.h"
#include "diskio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_TEMP_FILES 256

static char tempDir[1024];
static char* tempFiles[MAX_TEMP_FILES];
static int tempFileCount;

static void removeTempFiles(void) {
    int i;

    for(i = 0; i < tempFileCount; i++) {
        unlink(tempFiles[i]);
        free(tempFiles[i]);
    }

    if(tempDir[0])
        rmdir(tempDir);
}

char* tempPath(const char* name) {
    char* path;
    int i;

    if(!tempDir[0]) {
        const char* base = getenv("TMPDIR");
        snprintf(tempDir, sizeof(tempDir), "%s/fsman-test-XXXXXX", base ? base : "/tmp");
        if(!mkdtemp(tempDir)) {
            perror("mkdtemp");
            exit(EXIT_FAILURE);
        }
        atexit(removeTempFiles);
    }

    for(i = 0; i < tempFileCount; i++) {
        if(strcmp(strrchr(tempFiles[i], '/') + 1, name) == 0)
            return tempFiles[i];
    }

    if(tempFileCount == MAX_TEMP_FILES) {
        fprintf(stderr, "too many temporary files\n");
        exit(EXIT_FAILURE);
    }

    path = malloc(strlen(tempDir) + strlen(name) + 2);
    sprintf(path, "%s/%s", tempDir, name);
    tempFiles[tempFileCount++] = path;

    return path;
}

int writeHostFile(const char* path, const void* data, size_t size) {
    FILE* file = fopen(path, "wb");
    size_t written;

    if(!file)
        return 0;

    written = fwrite(data, 1, size, file);
    fclose(file);

    return written == size;
}

long readHostFile(const char* path, unsigned char** outData) {
    FILE* file = fopen(path, "rb");
    long size;

    if(!file)
        return -1;

    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);

    *outData = malloc(size + 1);
    if(fread(*outData, 1, size, file) != (size_t)size) {
        free(*outData);
        fclose(file);
        return -1;
    }

    fclose(file);
    return size;
}

void fillPattern(unsigned char* buf, size_t size, unsigned int seed) {
    size_t i;

    for(i = 0; i < size; i++)
        buf[i] = (unsigned char)(i * 7 + i / SECTOR_SIZE * 13 + seed * 31 + 1);
}

Filesystem* mountBlankDisk(void) {
    char* path = tempPath("test.dsk");
    char* data = createDiskData();

    memcpy(getSectorData(data, 0, VOLUME_INFORMATION_SEGMENT), diskSignature, DISK_SIGNATURE_SIZE);
    saveDiskFile(path, data);
    freeDiskData(data);

    return mountFilesystem(path);
}

Filesystem* mountFormattedDisk(void) {
    Filesystem* fs = mountBlankDisk();

    if(fs)
        formatFilesystem(fs, "TESTVOL");

    return fs;
}

void discardDisk(Filesystem* fs) {
    if(fs)
        unmountFilesystem(fs, 0);
}

unsigned char* sectorBytes(Filesystem* fs, int track, int sector) {
    return (unsigned char*)&fs->diskData[(track * SECTORS_PER_TRACK + sector) * SECTOR_SIZE];
}

int bitmapAllocated(Filesystem* fs, int track, int sector) {
    int block = track * SECTORS_PER_TRACK + sector;

    return (sectorBytes(fs, 0, SPACE_BITMAP_SEGMENT)[block / 8] >> (block % 8)) & 1;
}

int allocatedDataSectors(Filesystem* fs) {
    int track, sector;
    int count = 0;

    for(track = 1; track < TRACKS; track++)
        for(sector = 0; sector < SECTORS_PER_TRACK; sector++)
            count += bitmapAllocated(fs, track, sector);

    return count;
}

void fillDisk(Filesystem* fs, int leaveFree) {
    unsigned char* bitmap = sectorBytes(fs, 0, SPACE_BITMAP_SEGMENT);
    int block;

    for(block = 0; block < SECTORS; block++) {
        if(bitmap[block / 8] & (1 << (block % 8)))
            continue;
        if(leaveFree > 0)
            leaveFree--;
        else
            bitmap[block / 8] |= 1 << (block % 8);
    }
}

unsigned char* rootEntryBytes(Filesystem* fs, int slot) {
    return &sectorBytes(fs, ROOT_FOLDER_TRACK, ROOT_FOLDER_SECTOR)[slot * sizeof(FsDirectoryEntry)];
}
