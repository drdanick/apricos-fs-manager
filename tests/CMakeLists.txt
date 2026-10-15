set (src ${PROJECT_SOURCE_DIR}/src)

# The filesystem code, without the command line front end
set (fsman_core
    ${src}/filesystem.c
    ${src}/diskio.c
    ${src}/allocator.c
    ${src}/fsdirectory.c
    ${src}/fsfile.c
    )

# Unit tests, one program per area
foreach (name filesystem allocator directory file)
    add_executable (test_${name} test_${name}.c testing.c fixture.c ${fsman_core})
    target_include_directories (test_${name} PRIVATE ${src})
    add_test (NAME ${name} COMMAND test_${name})
endforeach ()

# End-to-end tests of the command line, if pytest is available.
# Use the python3 on the PATH, as a shell would, rather than a framework install.
set (Python3_FIND_FRAMEWORK LAST)
find_package (Python3 COMPONENTS Interpreter)
if (Python3_FOUND)
    execute_process (COMMAND ${Python3_EXECUTABLE} -c "import pytest"
        RESULT_VARIABLE pytest_missing OUTPUT_QUIET ERROR_QUIET)
endif ()

if (Python3_FOUND AND NOT pytest_missing)
    add_test (NAME cli
        COMMAND ${Python3_EXECUTABLE} -m pytest -q -p no:cacheprovider system
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties (cli PROPERTIES
        ENVIRONMENT "APRICOSFSMAN=$<TARGET_FILE:apricosfsman>")
else ()
    message (STATUS "pytest not found: skipping the command line tests")
endif ()
