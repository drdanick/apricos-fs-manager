#ifndef FSDIRECTORY_H
#define FSDIRECTORY_H

#include "filesystem.h"

long long createDirectory(Filesystem* fs, char* name);
int createDirectoryAtBlock(Filesystem* fs, char* name, unsigned int blockNum);
FsDirectory openBlockAsDirectory(Filesystem* fs, unsigned int blockNum);
void closeDirectory(FsDirectory dir);

#endif /* FSDIRECTORY_H */
