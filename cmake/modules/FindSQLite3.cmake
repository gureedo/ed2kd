# - Find SQLite3
# Find the SQLite3 includes and library.
# Once done this will define
#
#  SQLITE3_INCLUDE_DIRS - where to find sqlite3.h, etc.
#  SQLITE3_LIBRARIES    - List of libraries when using SQLite3.
#  SQLITE3_FOUND        - True if SQLite3 found.
#
#  SQLITE3_VERSION_STRING - The version of SQLite3 found (x.y.z)
#  SQLITE3_VERSION_MAJOR  - The major version
#  SQLITE3_VERSION_MINOR  - The minor version
#  SQLITE3_VERSION_PATCH  - The patch version

FIND_PATH(SQLITE3_INCLUDE_DIR NAMES sqlite3.h)
FIND_LIBRARY(SQLITE3_LIBRARY  NAMES sqlite3)

MARK_AS_ADVANCED(SQLITE3_LIBRARY SQLITE3_INCLUDE_DIR)

IF(SQLITE3_INCLUDE_DIR AND EXISTS "${SQLITE3_INCLUDE_DIR}/sqlite3.h")
    # Read and parse version header file for version number
    file(READ "${SQLITE3_INCLUDE_DIR}/sqlite3.h" _sqlite3_HEADER_CONTENTS)

    string(REGEX REPLACE ".*#define SQLITE_VERSION +\"([0-9]+).*" "\\1" SQLITE3_VERSION_MAJOR "${_sqlite3_HEADER_CONTENTS}")
    string(REGEX REPLACE ".*#define SQLITE_VERSION +\"[0-9]+\\.([0-9]+).*" "\\1" SQLITE3_VERSION_MINOR "${_sqlite3_HEADER_CONTENTS}")
    string(REGEX REPLACE ".*#define SQLITE_VERSION +\"[0-9]+\\.[0-9]+\\.([0-9]+).*" "\\1" SQLITE3_VERSION_PATCH "${_sqlite3_HEADER_CONTENTS}")

    SET(SQLITE3_VERSION_STRING "${SQLITE3_VERSION_MAJOR}.${SQLITE3_VERSION_MINOR}.${SQLITE3_VERSION_PATCH}")
ENDIF()

# handle the QUIETLY and REQUIRED arguments and set SQLITE3_FOUND to TRUE if 
# all listed variables are TRUE
INCLUDE(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(SQLite3
    REQUIRED_VARS SQLITE3_LIBRARY SQLITE3_INCLUDE_DIR
    VERSION_VAR SQLITE3_VERSION_STRING
)

IF(SQLITE3_FOUND)
    SET(SQLITE3_INCLUDE_DIRS ${SQLITE3_INCLUDE_DIR})
    SET(SQLITE3_LIBRARIES ${SQLITE3_LIBRARY})
ENDIF()
