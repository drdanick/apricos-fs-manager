include (FindPkgConfig)

set (apricosfsman
    main.c
    commandproc.c
    filesystem.c
    diskio.c
    memdumper.c
    allocator.c
    fsdirectory.c
    fsfile.c
    argparser.c
    )

add_executable (${PROJECT_NAME} ${apricosfsman})

install (TARGETS apricosfsman DESTINATION bin)
