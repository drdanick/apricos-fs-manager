#ifndef FSFILE_H
#define FSFILE_H

#include "filesystem.h"

long long createFile(Filesystem* fs, FsDirectory* parentDir, char* name);
int createFileAtBlock(Filesystem* fs, FsDirectory* parentDir, unsigned int blockNum, char* name);
int openBlockAsFile(Filesystem* fs, unsigned int blockNum, char* fileName, FsFile* file);
int getFsFileFromEntry(Filesystem* fs, FsDirectoryEntry* entry, FsFile* file);
int getIndexOfFileBlock(FsFile* file, FsFileBlockPointer* blockPointer);
FsFileBlockPointer* getNextFileBlock(Filesystem* fs, FsFile* file, FsFileBlockPointer* currentBlockPointer);
FsFileBlockPointer* getLastAllocatedFileBlock(Filesystem* fs, FsFile* file);
int countFileBlocks(FsFile* file);
void truncateFileBlocks(Filesystem* fs, FsFile* file, int keepBlocks);
int allocateNewFileData(Filesystem* fs, FsFile* file, unsigned int numBytes, FsFileAllocatedSpacePointer* outBlocks, int maxBlocks);
long long allocateNewFileBlock(Filesystem* fs, FsFile* file);
int deleteAllFileBlocks(Filesystem* fs, FsFile* file);
long long appendDataFromFileStream(Filesystem* fs, FsFile* file, char* fileName);
long long writeFileDataToFileStream(Filesystem* fs, FsFile* file, char* fileName);

#endif /* FSFILE_H */
