#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <unistd.h>
#include "commandproc.h"
#include "apricosfsman.h"
#include "filesystem.h"
#include "memdumper.h"
#include "allocator.h"
#include "diskio.h"
#include "fsdirectory.h"
#include "fsfile.h"

char* strToLower(char* str) {
    int i = 0;
    for( ; str[i] != '\0'; i++)
        str[i] = tolower(str[i]);

    return str;
}

char* strToUpper(char* str) {
    int i = 0;
    for( ; str[i] != '\0'; i++)
        str[i] = toupper(str[i]);

    return str;
}

/*
 * The working directory of the mounted filesystem. If there isn't one, prints
 * why and returns NULL.
 */
FsDirectory* requireWorkingDirectory() {
    if(!globalFileSystem) {
        printf("No filesystem mounted!\n");
        return NULL;
    }

    if(!getWorkingDirectory(globalFileSystem)) {
        printf("Disk is not formatted!\n");
        return NULL;
    }

    return getWorkingDirectory(globalFileSystem);
}

void printStatus() {
    static char cwd[4096]; /* Buffer for current working dir */

    getcwd(cwd, sizeof(cwd));

    printf("Program status:\n");
    printf("\tHost Working Directory: %s\n", cwd);
    printf("\tDisk Image mounted: %s\n", globalFileSystem == NULL ? "None" : globalFileSystem->diskImagePath);

    if(globalFileSystem) {
        char* volname = globalFileSystem->volumeInfo->volumeName;
        printf("\tVolume name: %s\n", strlen(volname) < 1 ? "[Unformatted]" : volname);
        if(strlen(volname) >= 1) {
            printf("\t\tCurrent working directory: "); printPathString(globalFileSystem);
        }
    }

    printf("\n");
}

void mountCmd(char* args) {
    Filesystem* fs;

    if(args == NULL) {
        printf("Invalid file name\n");
        return;
    }

    fs = mountFilesystem(args);
    if(fs == NULL) {
        printf("Cannot load file \"%s\" as disk image\n", args);
        return;
    }

    globalFileSystem = fs;
    printf("%s mounted!\n", args);
}

void syncCmd() {
    if(globalFileSystem == NULL) {
        printf("No FileSystem mounted!\n");
        return;
    }

    syncFilesystem(globalFileSystem);

    printf("Data flushed to disk\n");
}

void unmountCmd() {
    if(globalFileSystem == NULL) {
        printf("No FileSystem mounted!\n");
        return;
    }

    unmountFilesystem(globalFileSystem, 1);
    globalFileSystem = NULL;
    printf("unmounted!\n");
}

void peekCmd(unsigned int track, unsigned int sector) {
    char* peekData;
    char* peekEnd;

    if(globalFileSystem == NULL) {
        printf("No FileSystem mounted\n");
        return;
    }

    peekData = getSectorData(globalFileSystem->diskData, track, sector);
    peekEnd = &peekData[SECTOR_SIZE];

    do {
        int i = 0;
        char* asciiData = peekData;
        printf("%03u  ", SECTOR_SIZE - (int)(peekEnd-peekData));

        for( ; i < PEEK_COLUMNS_PER_LINE; i++) {
            dumpMemoryAsHex(peekData, peekEnd, PEEK_BYTES_PER_COLUMN);

            peekData += PEEK_BYTES_PER_COLUMN;
            printf("  ");
        }
        printf("[");
        dumpMemoryAsASCII(asciiData, peekEnd, PEEK_BYTES_PER_COLUMN * PEEK_COLUMNS_PER_LINE);
        printf("]\n");
    } while(peekData < peekEnd);

}

void mkdirCmd(char* dirName) {
    FsDirectory* dir = getWorkingDirectory(globalFileSystem);

    if(!dirName || !dir) {
        return;
    }

    dirName = strToUpper(dirName);

    if(findDirEntryByName(dir, dirName)) {
        printf("Directory already exists!\n");
        return;
    }

    allocateAndAddEntryToDirectory(globalFileSystem, dir, dirName, 0);
}

void lsCmd() {
    if(!globalFileSystem || !getWorkingDirectory(globalFileSystem)) {
        return;
    }

    printf("Directory listing of ");
    printPathString(globalFileSystem);
    printf(":\n");
    printDirectoryListing(getWorkingDirectory(globalFileSystem), "\t");
}

int cdCmd(char* dir) {
    if(strcmp(".", dir) == 0) {
        return 1;
    }

    if(strcmp("..", dir) == 0) {
        if(globalFileSystem->currentPathUnit <= 1) {
            return 0;
        }
        popDirectoryFromStack(globalFileSystem);
        return 1;
    } else {
        FsDirectory* parentDir = getWorkingDirectory(globalFileSystem);
        FsDirectoryEntry* dirEntry = findDirEntryByName(parentDir, dir);
        FsDirectory childDir;

        if(!dirEntry) {
            return 0;
        }

        if(!getFsDirectoryFromEntry(globalFileSystem, dirEntry, &childDir)) {
            return 0;
        }

        pushDirectoryToStack(globalFileSystem, childDir);

        return 1;
    }
}

/*
 * Change to each directory in the rest of the command line, separated by '/'.
 * If any step fails, the working directory is left unchanged.
 */
void cdPathCmd() {
    char* dir;
    int error = 0;
    FsDirectory* pathStackCopy = savePathStack(globalFileSystem);
    int oldPathStackEndIndex = globalFileSystem->currentPathUnit;

    while(dir = strtok(NULL, "/\n"), dir != NULL) {
        if(!cdCmd(strToUpper(dir))) {
            error = 1;
            restorePathStack(globalFileSystem, pathStackCopy, oldPathStackEndIndex);
            break;
        }
    }

    if(!error) {
        freePathStack(pathStackCopy);
    } else {
        printf("Could not change to specified directory.\n");
    }
}

int toggleexecCmd(char* entryName) {
    entryName = strToUpper(entryName);
    FsDirectoryEntry* entry = findDirEntryByName(getWorkingDirectory(globalFileSystem), entryName);

    if(!entry || !(entry->markerAndTrackNum & DIR_ENTRY_TYPE_MASK)) {
        return 0;
    }

    entry->markerAndTrackNum ^= DIR_ENTRY_EXECUTABLE_MASK;

    return 1;
}

void rmCmd(char* entryName) {
    FsDirectoryEntry* entry;
    FsFile file;

    entryName = strToUpper(entryName);
    entry = findDirEntryByName(getWorkingDirectory(globalFileSystem), entryName);

    if(!entry) {
        printf("Specified entry does not exist\n");
        return;
    }

    if(!(entry->markerAndTrackNum & DIR_ENTRY_TYPE_MASK)) {
        /* entry s a directory, so make sure it's empty */
        FsDirectory dir;
        if(!getFsDirectoryFromEntry(globalFileSystem, entry, &dir)) {
            printf("Unnable to remove directory\n");
            return;
        }
        if(countDirectoryEntries(&dir) > 0) {
            printf("Cannot remove non-empty directory\n");
            return;
        }
    }

    /* If the entry is a file, we need to clear its allocated blocks */
    if(entry->markerAndTrackNum & DIR_ENTRY_TYPE_MASK){
        if(
            !(getFsFileFromEntry(globalFileSystem, entry, &file))
            || !(deleteAllFileBlocks(globalFileSystem, &file))){

            printf("Error removing entry\n");
            return;
        }
    }

    /* Free the metadata sector and remove the entry from the parent dir */
    if(
        !freeSector(globalFileSystem, entry->markerAndTrackNum & TRACK_ID_MASK, entry->sectorNum & SECTOR_ID_MASK)
        || !removeDirEntry(entry)){

        printf("Error removing entry\n");
        return;
    }
}

int touchCmd(char* fileName) {
    fileName = strToUpper(fileName);

    if(createFile(globalFileSystem, getWorkingDirectory(globalFileSystem), fileName) == -1) {
        return 0;
    }

    return 1;
}

void loadFileCmd(char* entryName, char* fileName) {
    long long dataRead;
    FsDirectoryEntry* entry;
    FsFile file;
    entryName = strToUpper(entryName);

    entry = findDirEntryByName(getWorkingDirectory(globalFileSystem), entryName);
    if(!entry) {
        printf("No such directory entry\n");
        return;
    }

    if(!(entry->markerAndTrackNum & DIR_ENTRY_TYPE_MASK)) {
        printf("Cannot load data into a directory\n");
        return;
    }

    if(!getFsFileFromEntry(globalFileSystem, entry, &file)) {
        printf("Unnable to load file entry\n");
        return;
    }

    dataRead = appendDataFromFileStream(globalFileSystem, &file, fileName);

    if(dataRead >= 0) {
        printf("Read %lld bytes\n", dataRead);
    } else {
        printf("Could not read from file\n");
    }
}

void saveFileCmd(char* entryName, char* fileName) {
    long long dataWritten;
    FsDirectoryEntry* entry;
    FsFile file;
    entryName = strToUpper(entryName);

    entry = findDirEntryByName(getWorkingDirectory(globalFileSystem), entryName);
    if(!entry) {
        printf("No such directory entry\n");
        return;
    }

    if(!(entry->markerAndTrackNum & DIR_ENTRY_TYPE_MASK)) {
        printf("Cannot save data from a directory\n");
        return;
    }

    if(!getFsFileFromEntry(globalFileSystem, entry, &file)) {
        printf("Unnable to load file entry for saving\n");
        return;
    }

    dataWritten = writeFileDataToFileStream(globalFileSystem, &file, fileName);

    if(dataWritten >= 0) {
        printf("Wrote %lld bytes\n", dataWritten);
    } else {
        printf("Could not write to file\n");
    }
}

int newdiskCmd(char* fileName) {
    char* diskData = createDiskData();
    VolumeInfo* volumeInfo;

    if(!diskData){
        return 0;
    }

    volumeInfo = (VolumeInfo*)getSectorData(diskData, 0, VOLUME_INFORMATION_SEGMENT);
    memcpy(volumeInfo->diskSignature, diskSignature, DISK_SIGNATURE_SIZE);

    if(saveDiskFile(fileName, diskData) != 1) {
        freeDiskData(diskData);
        return 0;
    }

    freeDiskData(diskData);
    return 1;
}

int processLine(char* line) {
    char* command;
    command = strtok(line, " \n");

    if(command == NULL) {
        command = "";
    }

    /* Execute the command */

    if(strcmp("exit", strToLower(command)) == 0) {
        return -1;
    }
    else if(strcmp("status", strToLower(command)) == 0) {
        printStatus();
    }
    else if(strcmp("mount", strToLower(command)) == 0) {
        mountCmd(strtok(NULL, " \n"));
    }
    else if(strcmp("sync", strToLower(command)) == 0) {
        syncCmd();
    }
    else if(strcmp("unmount", strToLower(command)) == 0) {
        unmountCmd();
    }
    else if(strcmp("peek", strToLower(command)) == 0) {
        char* peekarg;
        unsigned int track;
        unsigned int sector;

        peekarg = strtok(NULL, "\n");

        if(peekarg == NULL || sscanf(peekarg, "%u %u", &track, &sector) < 2) {
            printf("Argument size missmatch!\n");
        } else {
            if(track >= TRACKS || sector >= SECTORS_PER_TRACK) {
                printf("Invalid track/sector number\n");
            } else {
                peekCmd(track, sector);
            }
        }
    }
    else if(strcmp("format", strToLower(command)) == 0) {
        char* volnamearg = strtok(NULL, "\n");
        if(!globalFileSystem) {
            printf("No FileSystem mounted\n");
        } else {
            if(volnamearg == NULL) {
                printf("Missing volume name\n");
            } else if(strlen(volnamearg) > VOLUME_NAME_LENGTH) {
                printf("Given name exceeds maximum length of %d\n", VOLUME_NAME_LENGTH);
            } else if(globalFileSystem) {
                printf("Formatting... ");
                formatFilesystem(globalFileSystem, volnamearg);
                printf("Done!\n");
            }
        }
    }
    else if(strcmp("mkdir", strToLower(command)) == 0) {
        char* dirName = strtok(NULL, " \n");
        if(!requireWorkingDirectory()) {
            /* Already reported */
        } else if(dirName) {
            mkdirCmd(dirName);
        } else {
            printf("Invalid directory name.\n");
        }
    }
    else if(strcmp("ls", strToLower(command)) == 0 || strcmp("dir", strToLower(command)) == 0) {
        if(requireWorkingDirectory()) {
            lsCmd();
        }
    }
    else if(strcmp("cd", strToLower(command)) == 0) {
        if(requireWorkingDirectory()) {
            cdPathCmd();
        }
    }
    else if(strcmp("toggleexec", strToLower(command)) == 0) {
        char* entryName = strtok(NULL, " \n");
        if(!requireWorkingDirectory()) {
            /* Already reported */
        } else if(!entryName || strlen(entryName) == 0) {
            printf("Invalid directory entry\n");
        } else if(!toggleexecCmd(entryName)) {
            printf("Invalid directory entry\n");
        }
    }
    else if(strcmp("rm", strToLower(command)) == 0) {
        char* entryName = strtok(NULL, " \n");
        if(!requireWorkingDirectory()) {
            /* Already reported */
        } else if(!entryName || strlen(entryName) == 0) {
            printf("Invalid directory entry\n");
        } else {
            rmCmd(entryName);
        }
    }
    else if(strcmp("touch", strToLower(command)) == 0) {
        char* entryName = strtok(NULL, " \n");
        if(!requireWorkingDirectory()) {
            /* Already reported */
        } else if(!entryName || strlen(entryName) == 0) {
            printf("Invalid file name\n");
        } else if(!touchCmd(entryName)) {
            printf("Error creating file\n");
        }
    }
    else if(strcmp("loadfile", strToLower(command)) == 0) {
        char* entryName = strtok(NULL, " \n");
        char* fileName = strtok(NULL, " \n");
        if(!requireWorkingDirectory()) {
            /* Already reported */
        } else if(!entryName || !fileName || strlen(entryName) == 0 || strlen(fileName) == 0) {
            printf("Invalid entry and or filename\n");
        } else {
            loadFileCmd(entryName, fileName);
        }
    }
    else if(strcmp("savefile", strToLower(command)) == 0) {
        char* entryName = strtok(NULL, " \n");
        char* fileName = strtok(NULL, " \n");
        if(!requireWorkingDirectory()) {
            /* Already reported */
        } else if(!entryName || !fileName || strlen(entryName) == 0 || strlen(fileName) == 0) {
            printf("Invalid entry and or filename\n");
        } else {
            saveFileCmd(entryName, fileName);
        }
    }
    else if(strcmp("newdisk", strToLower(command)) == 0 || strcmp("makedisk", strToLower(command)) == 0) {
        char* fileName = strtok(NULL, " \n");
        if(!fileName) {
            printf("Invalid filename\n");
        } else if(!newdiskCmd(fileName)){
            printf("Error creating new disk image\n");
        } else {
            printf("New disk image '%s' written\n", fileName);
        }

    }
    else {
        printf("Unknown Command\n");
    }

    return 1;
}
