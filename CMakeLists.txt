cmake_minimum_required(VERSION 2.8.7)
project(ed2kd C)

if (CMAKE_BINARY_DIR STREQUAL ${CMAKE_SOURCE_DIR})
    message(FATAL_ERROR "Building in source dir prohibited. Remove CMakeCache.txt and try 'mkdir build && cd build && cmake ..'.")
endif ()

if (NOT CMAKE_BUILD_TYPE OR CMAKE_BUILD_TYPE EQUAL "")
    message(STATUS "No build type selected, default to RelWithDebInfo")
    set(CMAKE_BUILD_TYPE "RelWithDebInfo")
endif ()

if (NOT CMAKE_INSTALL_PREFIX)
    message(STATUS "No install prefix provided, default to /usr/local")
    set(CMAKE_INSTALL_PREFIX /usr/local)
endif ()

SET(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_SOURCE_DIR}/cmake/modules)

find_package(Threads REQUIRED)
find_package(Libevent REQUIRED core pthreads)
find_package(Libconfig 1.4.8 REQUIRED)
find_package(ZLIB REQUIRED)

set(INCLUDES
        "${CMAKE_SOURCE_DIR}/3rdparty"
        "${LIBEVENT_INCLUDE_DIRS}"
        "${LIBCONFIG_INCLUDE_DIRS}"
        "${ZLIB_INCLUDE_DIRS}"
        )

set(LIBS
        "${LIBCONFIG_LIBRARIES}"
        "${LIBEVENT_LIBRARIES}"
        "${ZLIB_LIBRARIES}"
        )

set(SOURCES
        src/client.c
        src/config.c
        src/job.c
        src/log.c
        src/main.c
        src/packet.c
        src/portcheck.c
        src/server.c
        src/listener.c
        src/util.c
        src/db_sqlite.c
        3rdparty/sqlite3/sqlite3.c
        )

set_source_files_properties(3rdparty/sqlite3/sqlite3.c PROPERTIES COMPILE_FLAGS -Wno-unused-parameter)

add_definitions(
        -DSQLITE_THREADSAFE=1
        -DSQLITE_ENABLE_FTS3_PARENTHESIS
        -DSQLITE_ENABLE_FTS4
        -DSQLITE_ENABLE_FTS4_UNICODE61
        -DSQLITE_OMIT_LOAD_EXTENSION
)

include_directories(${INCLUDES})
add_executable(ed2kd ${SOURCES})

find_library(M_LIB m)
list(APPEND LIBS ${M_LIB})
set_target_properties(ed2kd PROPERTIES LINK_FLAGS "-fopenmp")

set(CMAKE_C_FLAGS_RELEASE "-Ofast -flto -march=native -funroll-loops -DNDEBUG")
set(CMAKE_C_FLAGS_RELWITHDEBINFO "-g -Ofast -flto -march=native -funroll-loops -DNDEBUG")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu11 -Wall -Wextra -fno-fast-math ")

target_link_libraries(ed2kd ${LIBS})

install(TARGETS ed2kd
        RUNTIME DESTINATION bin
        )
