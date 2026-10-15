/*
 * Tests for directories (fsdirectory.c) and the directory stack
 * (filesystem.c).
 *
 * Entries are checked byte by byte against the spec: byte 0 is
 * in-use (bit 7), executable (bit 6), file (bit 5) and the track
 * (bits 4-0); byte 1 is the sector; bytes 2-7 are the NUL-padded name.
 */

#include "testing.h"
#include "fixture.h"
#include "allocator.h"
#include "fsdirectory.h"

#include <stdio.h>
#include <string.h>

static Filesystem* fs;
static FsDirectory* root;

static void setUp(void) {
    fs = mountFormattedDisk();
    root = getWorkingDirectory(fs);
}

static void tearDown(void) {
    discardDisk(fs);
}

/* Fill every free slot of the root directory with entries for files at 9/n */
static void fillRoot(void) {
    char name[8];
    int i;

    for(i = countDirectoryEntries(root); i < MAX_DIR_ENTRIES; i++) {
        sprintf(name, "F%d", i);
        addEntrytoDirectory(root, 9, i, name, 1);
    }
}


/*========================================================
 * Adding entries
 *========================================================
 */

static void addDirectoryEntryEncoding(void) {
    static const unsigned char expected[8] = { 0x82, 0x05, 'B', 'I', 'N', 0, 0, 0 };

    CHECK(root != NULL);
    CHECK_INT_EQ(1, addEntrytoDirectory(root, 2, 5, "BIN", 0));
    CHECK(memcmp(rootEntryBytes(fs, 0), expected, 8) == 0);
}

static void addFileEntryEncoding(void) {
    static const unsigned char expected[8] = { 0xBF, 0x3F, 'H', 'E', 'L', 'L', 'O', 0 };

    CHECK(root != NULL);
    CHECK_INT_EQ(1, addEntrytoDirectory(root, 31, 63, "HELLO", 1));
    CHECK(memcmp(rootEntryBytes(fs, 0), expected, 8) == 0);
}

static void addBlockEntryConvertsBlock(void) {
    CHECK(root != NULL);
    CHECK_INT_EQ(1, addBlockEntrytoDirectory(root, 3 * SECTORS_PER_TRACK + 7, "X", 1));
    CHECK_INT_EQ(0xA3, rootEntryBytes(fs, 0)[0]);
    CHECK_INT_EQ(0x07, rootEntryBytes(fs, 0)[1]);
}

static void sixCharacterNameHasNoTerminator(void) {
    CHECK(root != NULL);
    CHECK_INT_EQ(1, addEntrytoDirectory(root, 2, 0, "ABCDEF", 1));
    CHECK(memcmp(rootEntryBytes(fs, 0) + 2, "ABCDEF", 6) == 0);
    /* The next entry starts straight after */
    CHECK_INT_EQ(0, rootEntryBytes(fs, 1)[0]);
    CHECK(findDirEntryByName(root, "ABCDEF") == (FsDirectoryEntry*)rootEntryBytes(fs, 0));
}

static void longNamesAreTruncated(void) {
    CHECK(root != NULL);
    CHECK_INT_EQ(1, addEntrytoDirectory(root, 2, 0, "ABCDEFGH", 1));
    CHECK(memcmp(rootEntryBytes(fs, 0) + 2, "ABCDEF", 6) == 0);
}

static void truncatedNamesCannotCollide(void) {
    CHECK(root != NULL);
    CHECK_INT_EQ(1, addEntrytoDirectory(root, 2, 0, "ABCDEFG", 1));
    CHECK(addEntrytoDirectory(root, 2, 1, "ABCDEFH", 1) != 1);
    CHECK_INT_EQ(1, countDirectoryEntries(root));
}

static void reusedSlotHasCleanName(void) {
    CHECK(root != NULL);
    CHECK_INT_EQ(1, addEntrytoDirectory(root, 2, 0, "LONGER", 1));
    CHECK_INT_EQ(1, removeDirEntryByName(root, "LONGER"));
    CHECK_INT_EQ(1, addEntrytoDirectory(root, 2, 1, "AB", 1));
    CHECK(memcmp(rootEntryBytes(fs, 0) + 2, "AB\0\0\0\0", 6) == 0);
}

static void addUsesFirstFreeSlot(void) {
    CHECK(root != NULL);
    addEntrytoDirectory(root, 2, 0, "A", 1);
    addEntrytoDirectory(root, 2, 1, "B", 1);
    addEntrytoDirectory(root, 2, 2, "C", 1);
    removeDirEntryByName(root, "B");

    CHECK_INT_EQ(1, addEntrytoDirectory(root, 2, 3, "D", 1));
    CHECK(findDirEntryByName(root, "D") == (FsDirectoryEntry*)rootEntryBytes(fs, 1));
}

static void duplicateNameIsRejected(void) {
    CHECK(root != NULL);
    CHECK_INT_EQ(1, addEntrytoDirectory(root, 2, 0, "SAME", 1));
    CHECK(addEntrytoDirectory(root, 2, 1, "SAME", 0) != 1);
    CHECK_INT_EQ(1, countDirectoryEntries(root));
}

static void fullDirectoryIsRejected(void) {
    CHECK(root != NULL);
    fillRoot();
    CHECK_INT_EQ(MAX_DIR_ENTRIES, countDirectoryEntries(root));
    CHECK(addEntrytoDirectory(root, 2, 0, "MORE", 1) != 1);
    CHECK(findDirEntryByName(root, "MORE") == NULL);
}


/*========================================================
 * Finding and removing entries
 *========================================================
 */

static void findMatchesWholeName(void) {
    CHECK(root != NULL);
    addEntrytoDirectory(root, 2, 0, "ABCD", 1);
    CHECK(findDirEntryByName(root, "ABCD") != NULL);
    CHECK(findDirEntryByName(root, "ABC") == NULL);
    CHECK(findDirEntryByName(root, "ABCDE") == NULL);
    CHECK(findDirEntryByName(root, "abcd") == NULL);
}

static void findIgnoresFreeSlots(void) {
    CHECK(root != NULL);
    /* A free slot (bit 7 clear) with a leftover name */
    memcpy(rootEntryBytes(fs, 0), "\x02\x00GHOST\0", 8);
    CHECK(findDirEntryByName(root, "GHOST") == NULL);
    CHECK_INT_EQ(0, countDirectoryEntries(root));
}

static void removeClearsEntry(void) {
    CHECK(root != NULL);
    addEntrytoDirectory(root, 2, 0, "GONE", 1);
    CHECK_INT_EQ(1, removeDirEntryByName(root, "GONE"));
    CHECK_INT_EQ(0, rootEntryBytes(fs, 0)[0]);
    CHECK(memcmp(rootEntryBytes(fs, 0) + 2, "\0\0\0\0\0\0", 6) == 0);
    CHECK(findDirEntryByName(root, "GONE") == NULL);
}

static void removeMissingEntryFails(void) {
    CHECK(root != NULL);
    CHECK_INT_EQ(0, removeDirEntryByName(root, "NONE"));
    CHECK_INT_EQ(0, removeDirEntry((FsDirectoryEntry*)rootEntryBytes(fs, 3)));
}

static void countSkipsFreeSlots(void) {
    CHECK(root != NULL);
    addEntrytoDirectory(root, 2, 0, "A", 1);
    addEntrytoDirectory(root, 2, 1, "B", 0);
    addEntrytoDirectory(root, 2, 2, "C", 1);
    removeDirEntryByName(root, "A");
    CHECK_INT_EQ(2, countDirectoryEntries(root));
}


/*========================================================
 * Creating and opening directories
 *========================================================
 */

static void allocateAndAddCreatesEmptyDirectory(void) {
    FsDirectoryEntry* entry;
    int i;

    CHECK(root != NULL);
    /* Leftover data where the directory will go */
    memset(sectorBytes(fs, 1, 1), 0xFF, SECTOR_SIZE);

    CHECK_INT_EQ(1, allocateAndAddEntryToDirectory(fs, root, "BIN", 0));
    entry = findDirEntryByName(root, "BIN");
    CHECK(entry != NULL);
    CHECK_INT_EQ(0x81, (unsigned char)entry->markerAndTrackNum);
    CHECK_INT_EQ(0x01, entry->sectorNum);
    CHECK(bitmapAllocated(fs, 1, 1));
    for(i = 0; i < SECTOR_SIZE; i++)
        CHECK_INT_EQ(0, sectorBytes(fs, 1, 1)[i]);
}

static void allocateAndAddInFullDirectoryDoesNotLeak(void) {
    int allocated;

    CHECK(root != NULL);
    fillRoot();
    allocated = allocatedDataSectors(fs);

    CHECK(allocateAndAddEntryToDirectory(fs, root, "MORE", 0) != 1);
    CHECK_INT_EQ(allocated, allocatedDataSectors(fs));
}

static void allocateAndAddOnFullDiskFails(void) {
    CHECK(root != NULL);
    fillDisk(fs, 0);
    CHECK_INT_EQ(0, allocateAndAddEntryToDirectory(fs, root, "BIN", 0));
    CHECK_INT_EQ(0, countDirectoryEntries(root));
}

static void openChildDirectory(void) {
    FsDirectory child;

    CHECK(root != NULL);
    allocateAndAddEntryToDirectory(fs, root, "BIN", 0);
    CHECK_INT_EQ(1, getFsDirectoryFromEntry(fs, findDirEntryByName(root, "BIN"), &child));
    CHECK_INT_EQ(65, child.block);
    CHECK((unsigned char*)child.rawData == sectorBytes(fs, 1, 1));
    CHECK(strncmp(child.name, "BIN", 3) == 0);
}

static void openFileEntryAsDirectoryFails(void) {
    FsDirectory child;

    CHECK(root != NULL);
    allocateBlock(fs, 65);
    addEntrytoDirectory(root, 1, 1, "FILE", 1);
    CHECK_INT_EQ(0, getFsDirectoryFromEntry(fs, findDirEntryByName(root, "FILE"), &child));
}

static void openUnallocatedDirectoryFails(void) {
    FsDirectory child;

    CHECK(root != NULL);
    addEntrytoDirectory(root, 5, 5, "STALE", 0);
    CHECK_INT_EQ(0, getFsDirectoryFromEntry(fs, findDirEntryByName(root, "STALE"), &child));
}


/*========================================================
 * Directory stack
 *========================================================
 */

static void pushMakesWorkingDirectory(void) {
    FsDirectory child;

    CHECK(root != NULL);
    allocateAndAddEntryToDirectory(fs, root, "BIN", 0);
    getFsDirectoryFromEntry(fs, findDirEntryByName(root, "BIN"), &child);

    CHECK_INT_EQ(1, pushDirectoryToStack(fs, child));
    CHECK_INT_EQ(2, fs->currentPathUnit);
    CHECK_INT_EQ(65, getWorkingDirectory(fs)->block);
}

static void popReturnsTopDirectory(void) {
    FsDirectory child;
    FsDirectory popped;

    CHECK(root != NULL);
    allocateAndAddEntryToDirectory(fs, root, "BIN", 0);
    getFsDirectoryFromEntry(fs, findDirEntryByName(root, "BIN"), &child);
    pushDirectoryToStack(fs, child);

    popped = popDirectoryFromStack(fs);
    CHECK_INT_EQ(1, fs->currentPathUnit);
    CHECK_INT_EQ(SECTORS_PER_TRACK, getWorkingDirectory(fs)->block);
    CHECK_INT_EQ(65, popped.block);
}

static void popEmptyStack(void) {
    FsDirectory popped;

    CHECK(root != NULL);
    clearDirectoryStack(fs);
    popped = popDirectoryFromStack(fs);
    CHECK(popped.rawData == NULL);
    CHECK_INT_EQ(0, fs->currentPathUnit);
    CHECK(getWorkingDirectory(fs) == NULL);
}

static void pushStopsAtMaxDepth(void) {
    int i;

    CHECK(root != NULL);
    for(i = 1; i < MAX_PATH_DEPTH; i++)
        CHECK_INT_EQ(1, pushDirectoryToStack(fs, *root));

    CHECK(pushDirectoryToStack(fs, *root) != 1);
    CHECK_INT_EQ(MAX_PATH_DEPTH, fs->currentPathUnit);
}

static void saveAndRestorePathStack(void) {
    FsDirectory child;
    FsDirectory* saved;

    CHECK(root != NULL);
    allocateAndAddEntryToDirectory(fs, root, "BIN", 0);
    getFsDirectoryFromEntry(fs, findDirEntryByName(root, "BIN"), &child);
    pushDirectoryToStack(fs, child);

    saved = savePathStack(fs);
    resetDirectoryStack(fs);
    CHECK_INT_EQ(1, fs->currentPathUnit);

    restorePathStack(fs, saved, 2);
    CHECK_INT_EQ(2, fs->currentPathUnit);
    CHECK_INT_EQ(65, getWorkingDirectory(fs)->block);
}


static const TestCase tests[] = {
    TEST(addDirectoryEntryEncoding),
    TEST(addFileEntryEncoding),
    TEST(addBlockEntryConvertsBlock),
    TEST(sixCharacterNameHasNoTerminator),
    TEST(longNamesAreTruncated),
    TEST(truncatedNamesCannotCollide),
    TEST(reusedSlotHasCleanName),
    TEST(addUsesFirstFreeSlot),
    TEST(duplicateNameIsRejected),
    TEST(fullDirectoryIsRejected),
    TEST(findMatchesWholeName),
    TEST(findIgnoresFreeSlots),
    TEST(removeClearsEntry),
    TEST(removeMissingEntryFails),
    TEST(countSkipsFreeSlots),
    TEST(allocateAndAddCreatesEmptyDirectory),
    TEST(allocateAndAddInFullDirectoryDoesNotLeak),
    TEST(allocateAndAddOnFullDiskFails),
    TEST(openChildDirectory),
    TEST(openFileEntryAsDirectoryFails),
    TEST(openUnallocatedDirectoryFails),
    TEST(pushMakesWorkingDirectory),
    TEST(popReturnsTopDirectory),
    TEST(popEmptyStack),
    TEST(pushStopsAtMaxDepth),
    TEST(saveAndRestorePathStack),
};

int main(int argc, char** argv) {
    return runTests(tests, sizeof(tests) / sizeof(tests[0]), setUp, tearDown, argc > 1 ? argv[1] : NULL);
}
