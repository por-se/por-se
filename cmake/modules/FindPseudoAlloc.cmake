# Tries to find an install of the pseudoalloc library and header files
# [this file was copied and adapted from KLEE's FindZ3.cmake file]
#
# Once done this will define
#  PSEUDOALLOC_FOUND - BOOL: System has the pseudoalloc library installed
#  PSEUDOALLOC_INCLUDE_DIRS - LIST:The GMP include directories
#  PSEUDOALLOC_LIBRARIES - LIST:The libraries needed to use pseudoalloc
include(FindPackageHandleStandardArgs)

# Try to find libraries
find_library(PSEUDOALLOC_LIBRARIES
  NAMES pseudoalloc libpseudoalloc
  DOC "pseudoalloc libraries"
  HINTS "${CMAKE_PREFIX_PATH}/target/debug"
)
if (PSEUDOALLOC_LIBRARIES)
  message(STATUS "Found pseudoalloc libraries: \"${PSEUDOALLOC_LIBRARIES}\"")
else()
  message(STATUS "Could not find pseudoalloc libraries")
endif()

# Try to find headers
find_path(PSEUDOALLOC_INCLUDE_DIRS
  NAMES "pseudoalloc.h"
  DOC "pseudoalloc C header"
)
if (PSEUDOALLOC_INCLUDE_DIRS)
  message(STATUS "Found pseudoalloc include path: \"${PSEUDOALLOC_INCLUDE_DIRS}\"")
else()
  message(STATUS "Could not find pseudoalloc include path")
endif()

if (PSEUDOALLOC_LIBRARIES AND PSEUDOALLOC_INCLUDE_DIRS)
  cmake_push_check_state()
  set(CMAKE_REQUIRED_INCLUDES "${PSEUDOALLOC_INCLUDE_DIRS}")
  set(CMAKE_REQUIRED_LIBRARIES "${PSEUDOALLOC_LIBRARIES}")
  check_cxx_source_compiles("
    #include <pseudoalloc.h>
    int main() {
      pseudoalloc::pseudoalloc_new_mapping(0, 64);
      return 0;
    }"
    HAVE_PSEUDALLOC_READY
  )
  cmake_pop_check_state()

  if (HAVE_PSEUDALLOC_READY)
    # Handle QUIET and REQUIRED and check the necessary variables were set and if so
    # set ``PSEUDOALLOC_FOUND``
    find_package_handle_standard_args(PSEUDOALLOC DEFAULT_MSG PSEUDOALLOC_INCLUDE_DIRS PSEUDOALLOC_LIBRARIES)
  else()
    message(STATUS "Please correctly configure pseudoalloc")
  endif()
endif()
