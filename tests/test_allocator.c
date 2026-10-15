/*
 * Tests for the space bitmap allocator (allocator.c).
 *
 * Most tests start from a blank disk, whose bitmap is all zero. The
 * bitmap is checked byte by byte against the layout in the spec:
 * block b = track * 64 + sector is bit b % 8 of byte b / 8.
 */

#include "testing.h"
#include "fixture.h"
#include "allocator.h"

#include <string.h>

static Filesystem* fs;

static void setUp(void) {
    fs = mountBlankDisk();
}

static void tearDown(void) {
    discardDisk(fs);
}

static unsigned char* bitmap(void) {
    return sectorBytes(fs, 0, SPEC_BITMAP_SECTOR);
}


/*========================================================
 * Single blocks
 *========================================================
 */

static void allocateSetsBitFromSpec(void) {
    CHECK(fs != NULL);
    CHECK_INT_EQ(1, allocateBlock(fs, 0));
    CHECK_INT_EQ(0x01, bitmap()[0]);

    CHECK_INT_EQ(1, allocateBlock(fs, 9));
    CHECK_INT_EQ(0x02, bitmap()[1]);

    CHECK_INT_EQ(1, allocateBlock(fs, SECTORS - 1));
    CHECK_INT_EQ(0x80, bitmap()[255]);
}

static void allocateSectorUsesTrackAndSector(void) {
    CHECK(fs != NULL);
    CHECK_INT_EQ(1, allocateSector(fs, 1, 3));
    CHECK_INT_EQ(0x08, bitmap()[8]);
    CHECK(!isSectorFree(fs, 1, 3));
    CHECK(!isBlockFree(fs, 67));
}

static void allocatingTwiceFails(void) {
    CHECK(fs != NULL);
    CHECK_INT_EQ(1, allocateBlock(fs, 100));
    CHECK_INT_EQ(0, allocateBlock(fs, 100));
    CHECK(!isBlockFree(fs, 100));
}

static void allocateOutOfRangeFails(void) {
    CHECK(fs != NULL);
    CHECK_INT_EQ(0, allocateBlock(fs, SECTORS));
    /* The byte after the bitmap belongs to the root directory sector */
    CHECK_INT_EQ(0, sectorBytes(fs, 1, 0)[0]);
}

static void freeClearsOnlyThatBit(void) {
    CHECK(fs != NULL);
    memset(bitmap(), 0xFF, SECTOR_SIZE);
    CHECK_INT_EQ(1, freeBlock(fs, 70));
    CHECK_INT_EQ(0xBF, bitmap()[8]);
    CHECK_INT_EQ(0xFF, bitmap()[7]);
    CHECK_INT_EQ(0xFF, bitmap()[9]);
    CHECK(isBlockFree(fs, 70));
    CHECK(isSectorFree(fs, 1, 6));
}

static void freeSectorUsesTrackAndSector(void) {
    CHECK(fs != NULL);
    memset(bitmap(), 0xFF, SECTOR_SIZE);
    CHECK_INT_EQ(1, freeSector(fs, 31, 63));
    CHECK_INT_EQ(0x7F, bitmap()[255]);
}

static void freeOutOfRangeFails(void) {
    CHECK(fs != NULL);
    sectorBytes(fs, 1, 0)[0] = 0xFF;
    CHECK_INT_EQ(0, freeBlock(fs, SECTORS));
    CHECK_INT_EQ(0xFF, sectorBytes(fs, 1, 0)[0]);
}


/*========================================================
 * Searching
 *========================================================
 */

static void findNextFreeBlockSkipsAllocated(void) {
    int i;

    CHECK(fs != NULL);
    for(i = 0; i < 10; i++)
        allocateBlock(fs, i);

    CHECK_INT_EQ(10, findNextFreeBlock(fs, 0));
    CHECK_INT_EQ(20, findNextFreeBlock(fs, 20));
}

static void findNextFreeBlockOnFullDisk(void) {
    CHECK(fs != NULL);
    memset(bitmap(), 0xFF, SECTOR_SIZE);
    CHECK_INT_EQ(-1, findNextFreeBlock(fs, 0));
}

static void findNextFreeBlockFindsLastBlock(void) {
    CHECK(fs != NULL);
    memset(bitmap(), 0xFF, SECTOR_SIZE);
    freeBlock(fs, SECTORS - 1);
    CHECK_INT_EQ(SECTORS - 1, findNextFreeBlock(fs, 0));
}


/*========================================================
 * Automatic allocation
 *========================================================
 */

static void autoAllocateIsFirstFit(void) {
    discardDisk(fs);
    fs = mountFormattedDisk();
    CHECK(fs != NULL);

    CHECK_INT_EQ(65, autoAllocateBlock(fs));
    CHECK_INT_EQ(66, autoAllocateBlock(fs));
    freeBlock(fs, 65);
    CHECK_INT_EQ(65, autoAllocateBlock(fs));
}

static void autoAllocateOnFullDisk(void) {
    CHECK(fs != NULL);
    memset(bitmap(), 0xFF, SECTOR_SIZE);
    CHECK_INT_EQ(-1, autoAllocateBlock(fs));
}

static void autoAllocateBlocksFillsHolesFirst(void) {
    unsigned int blocks[3];

    discardDisk(fs);
    fs = mountFormattedDisk();
    CHECK(fs != NULL);
    allocateBlock(fs, 66);

    CHECK_INT_EQ(1, autoAllocateBlocks(fs, 3, blocks));
    CHECK_INT_EQ(65, blocks[0]);
    CHECK_INT_EQ(67, blocks[1]);
    CHECK_INT_EQ(68, blocks[2]);
    CHECK(!isBlockFree(fs, 65));
    CHECK(!isBlockFree(fs, 67));
    CHECK(!isBlockFree(fs, 68));
    CHECK(isBlockFree(fs, 69));
}

static void autoAllocateBlocksRollsBackWhenDiskFull(void) {
    unsigned int blocks[3];

    discardDisk(fs);
    fs = mountFormattedDisk();
    CHECK(fs != NULL);
    fillDisk(fs, 2);

    CHECK_INT_EQ(0, autoAllocateBlocks(fs, 3, blocks));
    CHECK(isBlockFree(fs, 65));
    CHECK(isBlockFree(fs, 66));
}

static void allocateBlocksRollsBackOnConflict(void) {
    unsigned int blocks[] = { 100, 101, 100 };

    CHECK(fs != NULL);
    CHECK_INT_EQ(0, allocateBlocks(fs, blocks, 3));
    CHECK(isBlockFree(fs, 100));
    CHECK(isBlockFree(fs, 101));
}

static void freeBlocksFreesEach(void) {
    unsigned int blocks[] = { 100, 200, 300 };

    CHECK(fs != NULL);
    CHECK_INT_EQ(1, allocateBlocks(fs, blocks, 3));
    CHECK_INT_EQ(1, freeBlocks(fs, blocks, 3));
    CHECK(isBlockFree(fs, 100));
    CHECK(isBlockFree(fs, 200));
    CHECK(isBlockFree(fs, 300));
}


static const TestCase tests[] = {
    TEST(allocateSetsBitFromSpec),
    TEST(allocateSectorUsesTrackAndSector),
    TEST(allocatingTwiceFails),
    TEST(allocateOutOfRangeFails),
    TEST(freeClearsOnlyThatBit),
    TEST(freeSectorUsesTrackAndSector),
    TEST(freeOutOfRangeFails),
    TEST(findNextFreeBlockSkipsAllocated),
    TEST(findNextFreeBlockOnFullDisk),
    TEST(findNextFreeBlockFindsLastBlock),
    TEST(autoAllocateIsFirstFit),
    TEST(autoAllocateOnFullDisk),
    TEST(autoAllocateBlocksFillsHolesFirst),
    TEST(autoAllocateBlocksRollsBackWhenDiskFull),
    TEST(allocateBlocksRollsBackOnConflict),
    TEST(freeBlocksFreesEach),
};

int main(int argc, char** argv) {
    return runTests(tests, sizeof(tests) / sizeof(tests[0]), setUp, tearDown, argc > 1 ? argv[1] : NULL);
}
