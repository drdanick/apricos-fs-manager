cmake_minimum_required (VERSION 3.15)
set(CMAKE_LEGACY_CYGWIN_WIN32 0) # Remove when CMake >= 2.8.4 is required

project(apricosfsman)

# Apricosfsman version
set (PROJECT_VERSION "0.1")

# Resource directory
#set ( PROJECT_RESOURCE_DIR "${CMAKE_INSTALL_PREFIX}/share/apricosterm" )

add_definitions(
    -DVERSION="${PROJECT_VERSION}"
    )

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" OR
    "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    set(warnings "-Wall -Wextra -Werror")
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
    set(warnings "/W4 /WX /EHsc")
endif()
if (NOT CONFIGURED_ONCE)
    set(CMAKE_CXX_FLAGS "${warnings}"
        CACHE STRING "Flags used by the compiler during all build types." FORCE)
    set(CMAKE_C_FLAGS   "${warnings}"
        CACHE STRING "Flags used by the compiler during all build types." FORCE)
endif()

# uninstall target
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/targets/cmake_uninstall.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/targets/cmake_uninstall.cmake"
    IMMEDIATE @ONLY)

add_custom_target(uninstall
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake)

add_subdirectory (src)

option (APRICOSFSMAN_BUILD_TESTS "Build the test suite" ON)
if (APRICOSFSMAN_BUILD_TESTS)
    enable_testing ()
    add_subdirectory (tests)
endif()
