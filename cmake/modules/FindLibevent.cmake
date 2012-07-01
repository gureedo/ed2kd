# - Find libevent
# Find the native libevent includes and library.
# Once done this will define
#
#  LIBEVENT_INCLUDE_DIRS - where to find event2/event.h, etc.
#  LIBEVENT_LIBRARIES    - List of libraries when using libevent.
#  LIBEVENT_FOUND        - True if libevent found.
#
#  LIBEVENT_VERSION_STRING - The version of libevent found (x.y.z)
#  LIBEVENT_VERSION_MAJOR  - The major version of libevent
#  LIBEVENT_VERSION_MINOR  - The minor version of libevent
#  LIBEVENT_VERSION_PATCH  - The patch version of libevent

FIND_PATH(LIBEVENT_INCLUDE_DIR NAMES event2/event.h)
FIND_LIBRARY(LIBEVENT_LIBRARY  NAMES event_core)

MARK_AS_ADVANCED(LIBEVENT_LIBRARY LIBEVENT_INCLUDE_DIR)

IF(LIBEVENT_INCLUDE_DIR AND EXISTS "${LIBEVENT_INCLUDE_DIR}/event2/event-config.h")
    # Read and parse libevent version header file for version number
    file(READ "${LIBEVENT_INCLUDE_DIR}/event2/event-config.h" _libevent_HEADER_CONTENTS)

    string(REGEX REPLACE ".*#define _EVENT_VERSION +\"([0-9]+).*" "\\1" LIBEVENT_VERSION_MAJOR "${_libevent_HEADER_CONTENTS}")
    string(REGEX REPLACE ".*#define _EVENT_VERSION +\"[0-9]+\\.([0-9]+).*" "\\1" LIBEVENT_VERSION_MINOR "${_libevent_HEADER_CONTENTS}")
    string(REGEX REPLACE ".*#define _EVENT_VERSION +\"[0-9]+\\.[0-9]+\\.([0-9]+).*" "\\1" LIBEVENT_VERSION_PATCH "${_libevent_HEADER_CONTENTS}")

    SET(LIBEVENT_VERSION_STRING "${LIBEVENT_VERSION_MAJOR}.${LIBEVENT_VERSION_MINOR}.${LIBEVENT_VERSION_PATCH}")
    SET(LIBEVENT_MAJOR_VERSION "${LIBEVENT_VERSION_MAJOR}")
    SET(LIBEVENT_MINOR_VERSION "${LIBEVENT_VERSION_MINOR}")
    SET(LIBEVENT_PATCH_VERSION "${LIBEVENT_VERSION_PATCH}")
ENDIF()

# handle the QUIETLY and REQUIRED arguments and set LIBEVENT_FOUND to TRUE if 
# all listed variables are TRUE
INCLUDE(${CMAKE_CURRENT_LIST_DIR}/FindPackageHandleStandardArgs.cmake)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(LIBEVENT
    REQUIRED_VARS LIBEVENT_LIBRARY LIBEVENT_INCLUDE_DIR
    VERSION_VAR LIBEVENT_VERSION_STRING
)

IF(LIBEVENT_FOUND)
    SET(LIBEVENT_INCLUDE_DIRS ${LIBEVENT_INCLUDE_DIR})
    SET(LIBEVENT_LIBRARIES ${LIBEVENT_LIBRARY})
ENDIF()

