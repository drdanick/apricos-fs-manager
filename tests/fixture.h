#ifndef FIXTURE_H
#define FIXTURE_H

/*
 * Helpers shared by the unit tests: disk images in a temporary directory,
 * and raw access to sectors so results can be checked against the
 * filesystem spec rather than against the code under test.
 */

#include <stddef.h>
#include "filesystem.h"

/* The path of a file in this run's temporary directory. Removed at exit. */
char* tempPath(const char* name);

/* Write size bytes to a host file. Returns 1 on success. */
int writeHostFile(const char* path, const void* data, size_t size);

/* Read a host file into a new buffer. Returns its size, or -1 on failure. */
long readHostFile(const char* path, unsigned char** outData);

/* Fill buf with a pattern that differs from block to block */
void fillPattern(unsigned char* buf, size_t size, unsigned int seed);

/* Write a blank disk image (signature only, like `newdisk`) and mount it */
Filesystem* mountBlankDisk(void);

/* A blank disk, mounted and formatted as TESTVOL */
Filesystem* mountFormattedDisk(void);

/* Unmount without saving */
void discardDisk(Filesystem* fs);

/* Raw bytes of a sector */
unsigned char* sectorBytes(Filesystem* fs, int track, int sector);

/* The bitmap bit for a sector, read as the spec describes */
int bitmapAllocated(Filesystem* fs, int track, int sector);

/* The number of allocated sectors outside track 0 */
int allocatedDataSectors(Filesystem* fs);

/* Mark every free sector allocated except the first leaveFree of them */
void fillDisk(Filesystem* fs, int leaveFree);

/* Raw bytes of the root directory's entries */
unsigned char* rootEntryBytes(Filesystem* fs, int slot);

#endif /* FIXTURE_H */
