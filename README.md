Apricos-Filesystem-Manager
==========================

Readme to come...


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
- `tests/system/test_cli.py` runs the `apricosfsman` command line end to end. It needs Python 3
  with pytest, and is skipped if CMake can't find them. To run it on its own:
  `APRICOSFSMAN=build/src/apricosfsman pytest tests/system`.


License
-------

See LICENCE for details.
