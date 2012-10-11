CMAKE_MINIMUM_REQUIRED(VERSION 2.8.7)
PROJECT(ed2kd C)

if(CMAKE_BINARY_DIR STREQUAL ${CMAKE_SOURCE_DIR})
        message( FATAL_ERROR "Building in source dir prohibited. Remove CMakeCache.txt and try 'mkdir build && cd build && cmake ..'." )
endif()

SET(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH}
    ${CMAKE_SOURCE_DIR}/cmake/modules)

FIND_PACKAGE(Libevent REQUIRED core pthreads)
FIND_PACKAGE(Libconfig 1.4.8 REQUIRED)
FIND_PACKAGE(ZLIB REQUIRED)

SET(INCLUDES ${LIBEVENT_INCLUDE_DIRS} ${LIBCONFIG_INCLUDE_DIRS} ${ZLIB_INCLUDE_DIRS})

SET(LIBS ${LIBCONFIG_LIBRARIES} ${LIBEVENT_LIBRARIES} ${ZLIB_LIBRARIES} )

SET(SOURCES
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
    src/sqlite3/sqlite3.c
    src/db_sqlite.c
)

ADD_DEFINITIONS(
    -DSQLITE_THREADSAFE=1
    -DSQLITE_ENABLE_FTS4
    -DSQLITE_ENABLE_FTS3_PARENTHESIS
    -DSQLITE_ENABLE_FTS4_UNICODE61
    -DSQLITE_OMIT_LOAD_EXTENSION
)

INCLUDE_DIRECTORIES(${INCLUDES})
ADD_EXECUTABLE(ed2kd ${SOURCES})

IF( CMAKE_COMPILER_IS_GNUCC )
    FIND_LIBRARY(M_LIB m)
    LIST(APPEND LIBS ${M_LIB})
    SET_TARGET_PROPERTIES(ed2kd PROPERTIES LINK_FLAGS "-fopenmp")
ENDIF()

TARGET_LINK_LIBRARIES(ed2kd ${LIBS})

INSTALL(TARGETS ed2kd
    RUNTIME DESTINATION bin
)