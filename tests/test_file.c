/*
 * Tests for files (fsfile.c).
 *
 * Data is loaded from host files as `loadfile` does, and read back
 * straight from the disk image following the spec: the metadata sector
 * holds the size (bytes 0-1, little-endian) and then a (track, sector)
 * pair for each data block, with track 0 marking an unused slot.
 */

#include "testing.h"
#include "fixture.h"
#include "allocator.h"
#include "fsdirectory.h"
#include "fsfile.h"

#include <stdlib.h>
#include <string.h>

#define MAX_FILE_SIZE (MAX_FILE_BLOCKS * SECTOR_SIZE)

static Filesystem* fs;
static FsDirectory* root;
static unsigned char* hostData;

/* Data written to and read from files */
static unsigned char data[MAX_FILE_SIZE + SECTOR_SIZE];
static unsigned char readBack[MAX_FILE_SIZE + SECTOR_SIZE];

static void setUp(void) {
    fs = mountFormattedDisk();
    root = getWorkingDirectory(fs);
    hostData = NULL;
}

static void tearDown(void) {
    discardDisk(fs);
    free(hostData);
}

/* Create a file in the root directory and open it. Returns its metadata block, or -1. */
static long long newFile(const char* name, FsFile* file) {
    long long block = createFile(fs, root, (char*)name);

    if(block < 0 || !getFsFileFromEntry(fs, findDirEntryByName(root, (char*)name), file))
        return -1;

    return block;
}

/* Append bytes to a file through a host file, as `loadfile` does */
static long long load(FsFile* file, const unsigned char* bytes, size_t size) {
    char* path = tempPath("input.bin");

    if(!writeHostFile(path, bytes, size))
        return -2;

    return appendDataFromFileStream(fs, file, path);
}

static unsigned int specSize(long long metadataBlock) {
    unsigned char* metadata = sectorBytes(fs, metadataBlock / SECTORS_PER_TRACK, metadataBlock % SECTORS_PER_TRACK);

    return metadata[0] | (metadata[1] << 8);
}

/* The number of used block slots in a file's metadata */
static int specBlockCount(long long metadataBlock) {
    unsigned char* metadata = sectorBytes(fs, metadataBlock / SECTORS_PER_TRACK, metadataBlock % SECTORS_PER_TRACK);
    int i;
    int count = 0;

    for(i = 0; i < MAX_FILE_BLOCKS; i++)
        count += metadata[2 + 2 * i] != 0;

    return count;
}

/* Read a file's contents into readBack, following its metadata. Returns the size. */
static unsigned int specRead(long long metadataBlock) {
    unsigned char* metadata = sectorBytes(fs, metadataBlock / SECTORS_PER_TRACK, metadataBlock % SECTORS_PER_TRACK);
    unsigned int offset = 0;
    int i;

    for(i = 0; i < MAX_FILE_BLOCKS; i++) {
        unsigned char track = metadata[2 + 2 * i];
        unsigned char sector = metadata[3 + 2 * i];

        if(track == 0)
            break;
        memcpy(&readBack[offset], sectorBytes(fs, track, sector & 0x3F), SECTOR_SIZE);
        offset += SECTOR_SIZE;
    }

    return specSize(metadataBlock);
}

/* 1 if every data block of a file is allocated, in tracks 1-31, and used once */
static int specBlocksValid(long long metadataBlock) {
    unsigned char* metadata = sectorBytes(fs, metadataBlock / SECTORS_PER_TRACK, metadataBlock % SECTORS_PER_TRACK);
    int i, j;

    for(i = 0; i < MAX_FILE_BLOCKS && metadata[2 + 2 * i]; i++) {
        int track = metadata[2 + 2 * i];
        int sector = metadata[3 + 2 * i];

        if(track >= TRACKS || sector >= SECTORS_PER_TRACK || !bitmapAllocated(fs, track, sector))
            return 0;
        if(track * SECTORS_PER_TRACK + sector == metadataBlock)
            return 0;
        for(j = 0; j < i; j++) {
            if(metadata[2 + 2 * j] == track && metadata[3 + 2 * j] == sector)
                return 0;
        }
    }

    return 1;
}

/* Load size bytes into a new file and check the result against the spec */
static int loadAndCheck(const char* name, unsigned int size) {
    FsFile file;
    long long metadata = newFile(name, &file);

    fillPattern(data, size, size);

    if(metadata < 0)
        return 0;
    if(load(&file, data, size) != size)
        return 0;
    if(specSize(metadata) != size)
        return 0;
    if(specBlockCount(metadata) != (int)((size + SECTOR_SIZE - 1) / SECTOR_SIZE))
        return 0;
    if(!specBlocksValid(metadata))
        return 0;

    specRead(metadata);
    return memcmp(readBack, data, size) == 0;
}


/*========================================================
 * Creating files
 *========================================================
 */

static void createFileMakesEntryAndMetadata(void) {
    FsDirectoryEntry* entry;
    int i;

    CHECK(root != NULL);
    memset(sectorBytes(fs, 1, 1), 0xFF, SECTOR_SIZE);

    CHECK_INT_EQ(65, createFile(fs, root, "HELLO"));
    entry = findDirEntryByName(root, "HELLO");
    CHECK(entry != NULL);
    CHECK_INT_EQ(0xA1, (unsigned char)entry->markerAndTrackNum);
    CHECK_INT_EQ(0x01, entry->sectorNum);
    CHECK(bitmapAllocated(fs, 1, 1));
    for(i = 0; i < SECTOR_SIZE; i++)
        CHECK_INT_EQ(0, sectorBytes(fs, 1, 1)[i]);
}

static void createFileInFullDirectoryFails(void) {
    char name[8];
    int i;

    CHECK(root != NULL);
    for(i = 0; i < MAX_DIR_ENTRIES; i++) {
        sprintf(name, "F%d", i);
        CHECK(createFile(fs, root, name) >= 0);
    }

    CHECK_INT_EQ(-1, createFile(fs, root, "MORE"));
    CHECK_INT_EQ(1 + MAX_DIR_ENTRIES, allocatedDataSectors(fs));
}

static void createFileWithExistingNameFails(void) {
    CHECK(root != NULL);
    CHECK(createFile(fs, root, "HELLO") >= 0);
    CHECK_INT_EQ(-1, createFile(fs, root, "HELLO"));
    CHECK_INT_EQ(2, allocatedDataSectors(fs));
}

static void openDirectoryEntryAsFileFails(void) {
    FsFile file;

    CHECK(root != NULL);
    allocateAndAddEntryToDirectory(fs, root, "BIN", 0);
    CHECK_INT_EQ(0, getFsFileFromEntry(fs, findDirEntryByName(root, "BIN"), &file));
}

static void openFileGivesItsName(void) {
    FsFile file;

    CHECK(root != NULL);
    CHECK(newFile("ABCDEF", &file) >= 0);
    CHECK(strcmp(file.name, "ABCDEF") == 0);
    CHECK((unsigned char*)file.rawData == sectorBytes(fs, 1, 1));
}


/*========================================================
 * Loading data
 *========================================================
 */

static void loadEmptyFile(void) {
    FsFile file;
    long long metadata;

    CHECK(root != NULL);
    metadata = newFile("EMPTY", &file);
    CHECK_INT_EQ(0, load(&file, data, 0));
    CHECK_INT_EQ(0, specSize(metadata));
    CHECK_INT_EQ(0, specBlockCount(metadata));
    CHECK_INT_EQ(2, allocatedDataSectors(fs));
}

static void loadOneByte(void) {
    CHECK(root != NULL);
    CHECK(loadAndCheck("ONE", 1));
}

static void loadPartialBlock(void) {
    CHECK(root != NULL);
    CHECK(loadAndCheck("PART", 255));
}

static void loadExactBlock(void) {
    CHECK(root != NULL);
    CHECK(loadAndCheck("BLOCK", 256));
}

static void loadJustOverBlock(void) {
    CHECK(root != NULL);
    CHECK(loadAndCheck("OVER", 257));
}

static void loadSeveralBlocks(void) {
    CHECK(root != NULL);
    CHECK(loadAndCheck("SEVRL", 1000));
}

static void loadMaximumSize(void) {
    CHECK(root != NULL);
    CHECK(loadAndCheck("MAX", MAX_FILE_SIZE));
}

static void loadSeveralFiles(void) {
    CHECK(root != NULL);
    CHECK(loadAndCheck("A", 300));
    CHECK(loadAndCheck("B", 700));
    CHECK(loadAndCheck("C", 5));
    /* 3 metadata sectors, 2 + 3 + 1 data sectors and the root */
    CHECK_INT_EQ(10, allocatedDataSectors(fs));
}

static void loadTooLargeFails(void) {
    FsFile file;

    CHECK(root != NULL);
    CHECK(newFile("BIG", &file) >= 0);
    CHECK_INT_EQ(-1, load(&file, data, MAX_FILE_SIZE + 1));
}

static void loadMissingHostFileFails(void) {
    FsFile file;

    CHECK(root != NULL);
    CHECK(newFile("F", &file) >= 0);
    CHECK_INT_EQ(-1, appendDataFromFileStream(fs, &file, tempPath("missing.bin")));
}

static void appendAfterWholeBlocks(void) {
    FsFile file;
    long long metadata;

    CHECK(root != NULL);
    metadata = newFile("APPEND", &file);
    fillPattern(data, 512, 3);
    CHECK_INT_EQ(256, load(&file, data, 256));
    CHECK_INT_EQ(256, load(&file, data + 256, 256));

    CHECK_INT_EQ(512, specRead(metadata));
    CHECK(memcmp(readBack, data, 512) == 0);
    CHECK(specBlocksValid(metadata));
}

static void appendAfterPartialBlock(void) {
    FsFile file;
    long long metadata;

    CHECK(root != NULL);
    metadata = newFile("APPEND", &file);
    fillPattern(data, 200, 4);
    CHECK_INT_EQ(100, load(&file, data, 100));
    CHECK_INT_EQ(100, load(&file, data + 100, 100));

    CHECK_INT_EQ(200, specRead(metadata));
    CHECK(memcmp(readBack, data, 200) == 0);
}

static void appendAcrossBlockBoundary(void) {
    FsFile file;
    long long metadata;

    CHECK(root != NULL);
    metadata = newFile("APPEND", &file);
    fillPattern(data, 400, 5);
    CHECK_INT_EQ(200, load(&file, data, 200));
    CHECK_INT_EQ(200, load(&file, data + 200, 200));

    CHECK_INT_EQ(400, specRead(metadata));
    CHECK_INT_EQ(2, specBlockCount(metadata));
    CHECK(memcmp(readBack, data, 400) == 0);
    CHECK(specBlocksValid(metadata));
}

static void appendLeavesTrackZeroAlone(void) {
    static unsigned char trackZero[SECTORS_PER_TRACK * SECTOR_SIZE];
    FsFile file;

    CHECK(root != NULL);
    CHECK(newFile("APPEND", &file) >= 0);
    fillPattern(data, 200, 6);
    load(&file, data, 100);

    memcpy(trackZero, sectorBytes(fs, 0, 0), sizeof(trackZero));
    load(&file, data + 100, 100);
    CHECK(memcmp(trackZero, sectorBytes(fs, 0, 0), sizeof(trackZero)) == 0);
}

static void loadOnFullDiskFailsCleanly(void) {
    static unsigned char before[DISK_SIZE];
    FsFile file;
    int track, sector;

    CHECK(root != NULL);
    CHECK(newFile("FULL", &file) >= 0);
    fillDisk(fs, 1);
    memcpy(before, sectorBytes(fs, 0, 0), DISK_SIZE);

    /* Zeros, so that data written over the bitmap would free sectors */
    memset(data, 0, 512);
    CHECK_INT_EQ(-1, load(&file, data, 512));

    /* Nothing allocated before may have been freed */
    for(track = 0; track < TRACKS; track++) {
        for(sector = 0; sector < SECTORS_PER_TRACK; sector++) {
            int block = track * SECTORS_PER_TRACK + sector;
            if(before[SPACE_BITMAP_SEGMENT * SECTOR_SIZE + block / 8] & (1 << (block % 8)))
                CHECK(bitmapAllocated(fs, track, sector));
        }
    }
    /* Nor may the boot code and volume information have changed */
    CHECK(memcmp(before, sectorBytes(fs, 0, 0), SPACE_BITMAP_SEGMENT * SECTOR_SIZE) == 0);
}


/*========================================================
 * Deleting data
 *========================================================
 */

static void deleteAllBlocksFreesData(void) {
    FsFile file;
    long long metadata;
    int i;

    CHECK(root != NULL);
    metadata = newFile("DEL", &file);
    fillPattern(data, 1000, 7);
    load(&file, data, 1000);
    CHECK_INT_EQ(6, allocatedDataSectors(fs));

    CHECK_INT_EQ(1, deleteAllFileBlocks(fs, &file));
    CHECK_INT_EQ(0, specSize(metadata));
    CHECK_INT_EQ(0, specBlockCount(metadata));
    for(i = 2; i < SECTOR_SIZE; i++)
        CHECK_INT_EQ(0, sectorBytes(fs, 1, 1)[i]);
    /* The root and the metadata sector remain */
    CHECK_INT_EQ(2, allocatedDataSectors(fs));
}

static void reloadAfterDelete(void) {
    FsFile file;
    long long metadata;

    CHECK(root != NULL);
    metadata = newFile("RELOAD", &file);
    fillPattern(data, 600, 8);
    load(&file, data, 1000);
    deleteAllFileBlocks(fs, &file);

    CHECK_INT_EQ(600, load(&file, data, 600));
    CHECK_INT_EQ(600, specRead(metadata));
    CHECK(memcmp(readBack, data, 600) == 0);
}


/*========================================================
 * Saving data
 *========================================================
 */

/* Load size bytes into a new file, save it to a host file and compare */
static int saveRoundTrip(const char* name, unsigned int size) {
    FsFile file;
    char* path = tempPath("output.bin");
    long savedSize;
    int same;

    fillPattern(data, size, size + 9);
    if(newFile(name, &file) < 0 || load(&file, data, size) != size)
        return 0;
    if(writeFileDataToFileStream(fs, &file, path) != size)
        return 0;

    savedSize = readHostFile(path, &hostData);
    same = savedSize == (long)size && memcmp(hostData, data, size) == 0;
    free(hostData);
    hostData = NULL;

    return same;
}

static void saveEmptyFile(void) {
    CHECK(root != NULL);
    CHECK(saveRoundTrip("EMPTY", 0));
}

static void savePartialBlock(void) {
    CHECK(root != NULL);
    CHECK(saveRoundTrip("PART", 100));
}

static void saveSeveralBlocks(void) {
    CHECK(root != NULL);
    CHECK(saveRoundTrip("SEVRL", 1000));
}

static void saveMaximumSize(void) {
    CHECK(root != NULL);
    CHECK(saveRoundTrip("MAX", MAX_FILE_SIZE));
}

static void saveFileStoredOnLaterTrack(void) {
    int block;

    CHECK(root != NULL);
    /* Fill the rest of track 1, so the file's data starts at track 2 */
    for(block = 66; block < 2 * SECTORS_PER_TRACK; block++)
        allocateBlock(fs, block);

    CHECK(saveRoundTrip("LATER", 300));
}

static void saveToUnwritablePathFails(void) {
    FsFile file;

    CHECK(root != NULL);
    CHECK(newFile("F", &file) >= 0);
    CHECK_INT_EQ(-1, writeFileDataToFileStream(fs, &file, tempPath("no-such-dir/out.bin")));
}


static const TestCase tests[] = {
    TEST(createFileMakesEntryAndMetadata),
    TEST(createFileInFullDirectoryFails),
    TEST(createFileWithExistingNameFails),
    TEST(openDirectoryEntryAsFileFails),
    TEST(openFileGivesItsName),
    TEST(loadEmptyFile),
    TEST(loadOneByte),
    TEST(loadPartialBlock),
    TEST(loadExactBlock),
    TEST(loadJustOverBlock),
    TEST(loadSeveralBlocks),
    TEST(loadMaximumSize),
    TEST(loadSeveralFiles),
    TEST(loadTooLargeFails),
    TEST(loadMissingHostFileFails),
    TEST(appendAfterWholeBlocks),
    TEST(appendAfterPartialBlock),
    TEST(appendAcrossBlockBoundary),
    TEST(appendLeavesTrackZeroAlone),
    TEST(loadOnFullDiskFailsCleanly),
    TEST(deleteAllBlocksFreesData),
    TEST(reloadAfterDelete),
    TEST(saveEmptyFile),
    TEST(savePartialBlock),
    TEST(saveSeveralBlocks),
    TEST(saveMaximumSize),
    TEST(saveFileStoredOnLaterTrack),
    TEST(saveToUnwritablePathFails),
};

int main(int argc, char** argv) {
    return runTests(tests, sizeof(tests) / sizeof(tests[0]), setUp, tearDown, argc > 1 ? argv[1] : NULL);
}
