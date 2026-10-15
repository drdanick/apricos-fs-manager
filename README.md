Apricos-Filesystem-Manager
==========================

`apricosfsman` creates and edits disk images for Apricot OS, the operating system for the Apricos
8-bit CPU. It runs on your computer (not on the Apricos), and gives you a small interactive shell
for making a disk, formatting it, creating directories, and copying files on and off it.

A disk image is a 512 KiB file: 32 tracks of 64 sectors, each 256 bytes. These are the disks the
Apricos simulator (`aprsim`) reads and writes, so this tool is how you prepare a disk before
booting it, or look at what a program left on it afterwards.


Dependencies
------------

- A C compiler
- CMake 3.15 or later
- Python 3.7 or later with pytest (optional; only needed for the command line tests)


Compiling
---------

From the base project directory, enter:
```
mkdir build
cd build
cmake ..
make
```

This places the binary at `build/src/apricosfsman`. To install it, enter the following as root:
```
make install
```

`make uninstall` removes it again.


Running
-------

```
apricosfsman [options] [diskimage]
```

| Option | Meaning |
| ------ | ------- |
| `--version` / `-v` | Print version information and exit |
| `--help` / `-h` | Print usage information and exit |

If you give a disk image, it is mounted straight away. Otherwise use the `mount` command.

The program reads one command per line. The prompt shows the mounted volume and the current
directory, e.g. `[MYDISK/BIN/]>`. Because commands come from standard input, you can also pipe a
script into it, which is handy for preparing disks automatically:
```
apricosfsman < make-disk.txt
```

**`exit` does not save.** Changes are made to a copy of the disk in memory, and are only written
to the image file by `sync` or `unmount`. Always `unmount` (or `sync`) before you `exit`.


Quick start
-----------

This session makes a new disk, puts a program in a `BIN` directory, and marks it executable:
```
> newdisk mydisk.dsk
New disk image 'mydisk.dsk' written
> mount mydisk.dsk
mydisk.dsk mounted!
[Unformatted]> format MYDISK
Formatting... Done!
[MYDISK/]> mkdir bin
[MYDISK/]> cd bin
[MYDISK/BIN/]> touch hello
[MYDISK/BIN/]> loadfile hello hello.bin
Read 9 bytes
[MYDISK/BIN/]> toggleexec hello
[MYDISK/BIN/]> ls
Directory listing of /BIN/:
	[fx] HELLO
[MYDISK/BIN/]> unmount
unmounted!
> exit
Bye!
```

In a listing, the first letter is `f` for a file or `d` for a directory, and the second is `x` if
the entry is executable.

`format` only rewrites the volume information, the space bitmap and the root directory. The boot
code in track 0 is kept, so you can format a copy of an OS image and it will still boot.


Commands
--------

Commands are not case sensitive, and file and directory names are converted to upper case.

### Disk images

| Command | Description |
| ------- | ----------- |
| `newdisk FILE` | Create a new, blank disk image (also `makedisk`). It still needs to be mounted and formatted. |
| `mount FILE` | Load a disk image. It must have the Apricot disk signature. |
| `format NAME` | Create an empty filesystem with a volume name of up to 8 characters. Everything on the disk except the boot code is lost. |
| `sync` | Write the mounted disk back to its image file. |
| `unmount` | Write the mounted disk back to its image file and unload it. |
| `status` | Show the host working directory, the mounted image, the volume name and the current directory. |

### Directories

| Command | Description |
| ------- | ----------- |
| `ls` | List the current directory (also `dir`). |
| `cd PATH` | Change directory. `PATH` is one or more names separated by `/`, and may use `..` and `.`. |
| `mkdir NAME` | Create a directory. |

### Files

| Command | Description |
| ------- | ----------- |
| `touch NAME` | Create an empty file. |
| `loadfile NAME HOSTFILE` | Append the bytes of a file on your computer to a file on the disk. The data is copied exactly; for a program, that includes its `PB` header. |
| `savefile NAME HOSTFILE` | Copy a file from the disk to your computer. |
| `toggleexec NAME` | Turn a file's executable flag on or off. |
| `rm NAME` | Delete a file, or an empty directory. |

### Other

| Command | Description |
| ------- | ----------- |
| `peek TRACK SECTOR` | Show one sector of the disk in hex and ASCII. |
| `exit` | Quit, **without saving**. |


Limits
------

- Names are 1 to 6 characters. Longer names are cut short to 6 characters without warning.
- A directory holds up to 32 entries.
- A file can be at most 32,512 bytes (127 sectors).
- A disk has 31 tracks (1,984 sectors, 496 KiB) for files and directories. Track 0 is reserved.


Disk format
-----------

The disk and filesystem format is described in the
[Apricot OS documentation](https://github.com/drdanick/apricot-os-documentation)
(`filesystem/filesystem.tex`).


Known issues
------------

The tests in `tests/` found these bugs, and the tests that cover them currently fail:

- Loading data onto a nearly full disk can overwrite the space bitmap, and reports success.
- Appending to a file whose last sector is only partly full loses data and writes into track 0.
- `savefile` can cut files short, depending on where their data was stored.
- `touch` with a name that already exists, or `mkdir` in a full directory, wastes a sector.
- Two long names that are the same in their first 6 characters both get created, as duplicates.
- `cd`, `touch`, `loadfile` and `savefile` crash if no disk is mounted.
- `newdisk` says the image was written even when it couldn't be.

Until these are fixed, the safe way to put a file on a disk is to `touch` it and then `loadfile`
it once, on a disk with plenty of free space.


Testing
-------

The tests are built along with the program (turn them off with `-DAPRICOSFSMAN_BUILD_TESTS=OFF`).
Run them from the build directory with:
```
ctest --output-on-failure
```

- `tests/test_*.c` are unit tests of the filesystem code: disk images and formatting, the space
  bitmap allocator, directories, and files. They check the bytes written against the filesystem
  spec. Each builds to a program in `build/tests`, which takes an optional argument to run only the
  tests whose names contain it.
- `tests/system/test_cli.py` runs the `apricosfsman` command line end to end. It needs Python 3.7
  or later with pytest, and is skipped if CMake can't find them. To run it on its own:
  `APRICOSFSMAN=build/src/apricosfsman pytest tests/system`.


License
-------

See LICENSE for details.
