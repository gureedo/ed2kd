# - Find libatomic_ops
# Find the native libatomic_ops includes and library.
# Once done this will define
#
#  LIBATOMIC_OPS_INCLUDE_DIRS - where to find atomic_ops.h, etc.
#  LIBATOMIC_OPS_LIBRARIES    - List of libraries when using libatomic_ops.
#  LIBATOMIC_OPS_FOUND        - True if libatomic_ops found.
#
#  LIBATOMIC_OPS_VERSION_STRING - The version of libatomic_ops found (x.y.z)
#  LIBATOMIC_OPS_VERSION_MAJOR  - The major version
#  LIBATOMIC_OPS_VERSION_MINOR  - The minor version
#  LIBATOMIC_OPS_VERSION_PATCH  - The patch version

FIND_PATH(LIBATOMIC_OPS_INCLUDE_DIR NAMES atomic_ops.h)
FIND_LIBRARY(LIBATOMIC_OPS_LIBRARY  NAMES atomic_ops)

MARK_AS_ADVANCED(LIBATOMIC_OPS_LIBRARY LIBATOMIC_OPS_INCLUDE_DIR)

IF(LIBATOMIC_OPS_INCLUDE_DIR)

    IF(EXISTS "${LIBATOMIC_OPS_INCLUDE_DIR}/atomic_ops/ao_version.h")
        # Read and parse version header file for version number
        file(READ "${LIBATOMIC_OPS_INCLUDE_DIR}/atomic_ops/ao_version.h" _libatomic_ops_HEADER_CONTENTS)
        string(REGEX REPLACE ".*#define AO_VERSION_MAJOR +([0-9]+).*" "\\1" LIBATOMIC_OPS_VERSION_MAJOR "${_libatomic_ops_HEADER_CONTENTS}")
        string(REGEX REPLACE ".*#define AO_VERSION_MINOR +([0-9]+).*" "\\1" LIBATOMIC_OPS_VERSION_MINOR "${_libatomic_ops_HEADER_CONTENTS}")
        string(REGEX REPLACE ".*#define AO_VERSION_PATCH +([0-9]+).*" "\\1" LIBATOMIC_OPS_VERSION_PATCH "${_libatomic_ops_HEADER_CONTENTS}")
    ELSE()
        SET(LIBATOMIC_OPS_VERSION_MAJOR 0)
        SET(LIBATOMIC_OPS_VERSION_MINOR 0)
        SET(LIBATOMIC_OPS_VERSION_PATCH 0)
    ENDIF()

    SET(LIBATOMIC_OPS_VERSION_STRING "${LIBATOMIC_OPS_VERSION_MAJOR}.${LIBATOMIC_OPS_VERSION_MINOR}.${LIBATOMIC_OPS_VERSION_PATCH}")
ENDIF()

# handle the QUIETLY and REQUIRED arguments and set LIBATOMIC_OPS_FOUND to TRUE if
# all listed variables are TRUE
INCLUDE(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(Libatomic_ops
    REQUIRED_VARS LIBATOMIC_OPS_LIBRARY LIBATOMIC_OPS_INCLUDE_DIR
    VERSION_VAR LIBATOMIC_OPS_VERSION_STRING
)

IF(LIBATOMIC_OPS_FOUND)
    SET(LIBATOMIC_OPS_INCLUDE_DIRS ${LIBATOMIC_OPS_INCLUDE_DIR})
    SET(LIBATOMIC_OPS_LIBRARIES ${LIBATOMIC_OPS_LIBRARY})
ENDIF()
