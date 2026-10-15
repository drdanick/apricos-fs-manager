/*
 * Tests for disk image I/O (diskio.c) and mounting and formatting
 * (filesystem.c).
 */

#include "testing.h"
#include "fixture.h"
#include "diskio.h"
#include "filesystem.h"

#include <stdlib.h>
#include <string.h>

static Filesystem* fs;
static char* diskData;
static unsigned char* hostData;

static void setUp(void) {
    fs = NULL;
    diskData = NULL;
    hostData = NULL;
}

static void tearDown(void) {
    discardDisk(fs);
    freeDiskData(diskData);
    free(hostData);
}

static unsigned char* volumeInfo(void) {
    return sectorBytes(fs, 0, SPEC_VOLUME_SECTOR);
}


/*========================================================
 * Disk images
 *========================================================
 */

static void sectorOffsets(void) {
    diskData = createDiskData();
    CHECK(getSectorData(diskData, 0, 0) == diskData);
    CHECK(getSectorData(diskData, 0, 1) == diskData + SECTOR_SIZE);
    CHECK(getSectorData(diskData, 1, 0) == diskData + SECTORS_PER_TRACK * SECTOR_SIZE);
    CHECK(getSectorData(diskData, 31, 63) == diskData + DISK_SIZE - SECTOR_SIZE);
}

static void sectorNumbersWrap(void) {
    diskData = createDiskData();
    CHECK(getSectorData(diskData, TRACKS, 0) == diskData);
    CHECK(getSectorData(diskData, 0, SECTORS_PER_TRACK) == diskData);
}

static void blockNumbersAreTrackMajor(void) {
    diskData = createDiskData();
    CHECK(getBlockData(diskData, 0) == getSectorData(diskData, 0, 0));
    CHECK(getBlockData(diskData, 65) == getSectorData(diskData, 1, 1));
    CHECK(getBlockData(diskData, SECTORS - 1) == getSectorData(diskData, 31, 63));
}

static void newDiskDataIsZeroed(void) {
    int i;

    diskData = createDiskData();
    CHECK(diskData != NULL);
    for(i = 0; i < DISK_SIZE; i++)
        CHECK_INT_EQ(0, diskData[i]);
}

static void saveAndLoadRoundTrip(void) {
    char* path = tempPath("roundtrip.dsk");
    char* loaded;
    int same;

    diskData = createDiskData();
    fillPattern((unsigned char*)diskData, DISK_SIZE, 1);
    CHECK_INT_EQ(1, saveDiskFile(path, diskData));

    loaded = loadDataFromDiskFile(path);
    CHECK(loaded != NULL);
    same = memcmp(loaded, diskData, DISK_SIZE) == 0;
    freeDiskData(loaded);
    CHECK(same);
}

static void loadMissingFileFails(void) {
    CHECK(loadDataFromDiskFile(tempPath("missing.dsk")) == NULL);
}

static void loadShortFileFails(void) {
    char* path = tempPath("short.dsk");

    diskData = createDiskData();
    CHECK(writeHostFile(path, diskData, DISK_SIZE - 1));
    CHECK(loadDataFromDiskFile(path) == NULL);
}

static void saveToUnwritablePathFails(void) {
    diskData = createDiskData();
    CHECK_INT_EQ(-1, saveDiskFile(tempPath("no-such-dir/disk.dsk"), diskData));
}


/*========================================================
 * Mounting
 *========================================================
 */

static void mountRequiresSignature(void) {
    char* path = tempPath("unsigned.dsk");

    diskData = createDiskData();
    CHECK_INT_EQ(1, saveDiskFile(path, diskData));
    CHECK(mountFilesystem(path) == NULL);
}

static void mountRequiresSignatureInLastSector(void) {
    char* path = tempPath("misplaced.dsk");

    /* The signature belongs in sector 64 of track 1; sector 63 is the bitmap */
    diskData = createDiskData();
    memcpy(getSectorData(diskData, 0, SPEC_BITMAP_SECTOR), diskSignature, DISK_SIGNATURE_SIZE);
    CHECK_INT_EQ(1, saveDiskFile(path, diskData));
    CHECK(mountFilesystem(path) == NULL);
}

static void mountMissingFileFails(void) {
    CHECK(mountFilesystem(tempPath("missing.dsk")) == NULL);
}

static void mountUnformattedDisk(void) {
    fs = mountBlankDisk();
    CHECK(fs != NULL);
    CHECK_INT_EQ(0, volumeInfo()[4]);
    /* There is no root directory to open */
    CHECK(getWorkingDirectory(fs) == NULL);
}

static void unmountWithSaveWritesImage(void) {
    long size;

    fs = mountFormattedDisk();
    CHECK(fs != NULL);
    sectorBytes(fs, 5, 5)[17] = 0x5A;
    unmountFilesystem(fs, 1);
    fs = NULL;

    size = readHostFile(tempPath("test.dsk"), &hostData);
    CHECK_INT_EQ(DISK_SIZE, size);
    CHECK_INT_EQ(0x5A, hostData[(5 * SECTORS_PER_TRACK + 5) * SECTOR_SIZE + 17]);
}

static void unmountWithoutSaveKeepsImage(void) {
    fs = mountFormattedDisk();
    CHECK(fs != NULL);
    unmountFilesystem(fs, 1);

    fs = mountFilesystem(tempPath("test.dsk"));
    CHECK(fs != NULL);
    sectorBytes(fs, 5, 5)[17] = 0x5A;
    unmountFilesystem(fs, 0);
    fs = NULL;

    CHECK_INT_EQ(DISK_SIZE, readHostFile(tempPath("test.dsk"), &hostData));
    CHECK_INT_EQ(0, hostData[(5 * SECTORS_PER_TRACK + 5) * SECTOR_SIZE + 17]);
}

static void syncWritesImage(void) {
    fs = mountFormattedDisk();
    CHECK(fs != NULL);
    sectorBytes(fs, 2, 9)[0] = 0x77;
    syncFilesystem(fs);

    CHECK_INT_EQ(DISK_SIZE, readHostFile(tempPath("test.dsk"), &hostData));
    CHECK_INT_EQ(0x77, hostData[(2 * SECTORS_PER_TRACK + 9) * SECTOR_SIZE]);
}

static void remountedDiskOpensRoot(void) {
    fs = mountFormattedDisk();
    CHECK(fs != NULL);
    unmountFilesystem(fs, 1);

    fs = mountFilesystem(tempPath("test.dsk"));
    CHECK(fs != NULL);
    CHECK(getWorkingDirectory(fs) != NULL);
    CHECK_INT_EQ(SECTORS_PER_TRACK, getWorkingDirectory(fs)->block);
    CHECK_INT_EQ(1, fs->currentPathUnit);
}


/*========================================================
 * Formatting
 *========================================================
 */

static void formatWritesVolumeInfo(void) {
    static const unsigned char expected[12] = {
        0xC0, 0x30, 0x0C, 0x03, 'T', 'E', 'S', 'T', 'V', 'O', 'L', 0
    };

    fs = mountFormattedDisk();
    CHECK(fs != NULL);
    CHECK(memcmp(volumeInfo(), expected, sizeof(expected)) == 0);
    CHECK_INT_EQ(0xAA, volumeInfo()[254]);
    CHECK_INT_EQ(0x55, volumeInfo()[255]);
}

static void formatEightCharacterName(void) {
    fs = mountBlankDisk();
    CHECK(fs != NULL);
    formatFilesystem(fs, "ABCDEFGH");
    CHECK(memcmp(volumeInfo() + 4, "ABCDEFGH", 8) == 0);
    CHECK_INT_EQ(0, volumeInfo()[12]);
}

static void reformatReplacesName(void) {
    fs = mountBlankDisk();
    CHECK(fs != NULL);
    formatFilesystem(fs, "LONGNAME");
    formatFilesystem(fs, "AB");
    CHECK(memcmp(volumeInfo() + 4, "AB\0\0\0\0\0\0", 8) == 0);
}

static void formatReservesTrackZeroAndRoot(void) {
    unsigned char* bitmap;
    int i;

    fs = mountFormattedDisk();
    CHECK(fs != NULL);
    bitmap = sectorBytes(fs, 0, SPEC_BITMAP_SECTOR);

    for(i = 0; i < 8; i++)
        CHECK_INT_EQ(0xFF, bitmap[i]);
    CHECK_INT_EQ(0x01, bitmap[8]);
    for(i = 9; i < SECTOR_SIZE; i++)
        CHECK_INT_EQ(0, bitmap[i]);
}

static void formatCreatesEmptyRoot(void) {
    FsDirectory* root;
    unsigned char* rootSector;
    int i;

    fs = mountBlankDisk();
    CHECK(fs != NULL);
    memset(sectorBytes(fs, ROOT_FOLDER_TRACK, ROOT_FOLDER_SECTOR), 0xFF, SECTOR_SIZE);
    formatFilesystem(fs, "TESTVOL");

    root = getWorkingDirectory(fs);
    rootSector = sectorBytes(fs, ROOT_FOLDER_TRACK, ROOT_FOLDER_SECTOR);
    CHECK(root != NULL);
    CHECK_INT_EQ(1, fs->currentPathUnit);
    CHECK_INT_EQ(SECTORS_PER_TRACK, root->block);
    CHECK((unsigned char*)root->rawData == rootSector);
    for(i = 0; i < SECTOR_SIZE; i++)
        CHECK_INT_EQ(0, rootSector[i]);
}

static void formatKeepsBootCode(void) {
    unsigned char bootCode[62 * SECTOR_SIZE];

    fs = mountBlankDisk();
    CHECK(fs != NULL);
    fillPattern(bootCode, sizeof(bootCode), 2);
    memcpy(sectorBytes(fs, 0, 0), bootCode, sizeof(bootCode));

    formatFilesystem(fs, "TESTVOL");
    CHECK(memcmp(sectorBytes(fs, 0, 0), bootCode, sizeof(bootCode)) == 0);
}

static void formatFreesOldAllocations(void) {
    fs = mountFormattedDisk();
    CHECK(fs != NULL);
    fillDisk(fs, 0);
    formatFilesystem(fs, "TESTVOL");
    CHECK_INT_EQ(1, allocatedDataSectors(fs));
}


static const TestCase tests[] = {
    TEST(sectorOffsets),
    TEST(sectorNumbersWrap),
    TEST(blockNumbersAreTrackMajor),
    TEST(newDiskDataIsZeroed),
    TEST(saveAndLoadRoundTrip),
    TEST(loadMissingFileFails),
    TEST(loadShortFileFails),
    TEST(saveToUnwritablePathFails),
    TEST(mountRequiresSignature),
    TEST(mountRequiresSignatureInLastSector),
    TEST(mountMissingFileFails),
    TEST(mountUnformattedDisk),
    TEST(unmountWithSaveWritesImage),
    TEST(unmountWithoutSaveKeepsImage),
    TEST(syncWritesImage),
    TEST(remountedDiskOpensRoot),
    TEST(formatWritesVolumeInfo),
    TEST(formatEightCharacterName),
    TEST(reformatReplacesName),
    TEST(formatReservesTrackZeroAndRoot),
    TEST(formatCreatesEmptyRoot),
    TEST(formatKeepsBootCode),
    TEST(formatFreesOldAllocations),
};

int main(int argc, char** argv) {
    return runTests(tests, sizeof(tests) / sizeof(tests[0]), setUp, tearDown, argc > 1 ? argv[1] : NULL);
}
