# Tries to find an install of the CryptoPP library and header files
# [this file was copied and adapted from KLEE's FindZ3.cmake file]
#
# Once done this will define
#  CRYPTOPP_FOUND - BOOL: System has the CryptoPP library installed
#  CRYPTOPP_INCLUDE_DIRS - LIST:The GMP include directories
#  CRYPTOPP_LIBRARIES - LIST:The libraries needed to use CryptoPP
include(FindPackageHandleStandardArgs)

# Try to find libraries
find_library(CRYPTOPP_LIBRARIES
  NAMES cryptopp
  DOC "CryptoPP libraries"
)
if (CRYPTOPP_LIBRARIES)
  message(STATUS "Found CryptoPP libraries: \"${CRYPTOPP_LIBRARIES}\"")
else()
  message(STATUS "Could not find CryptoPP libraries")
endif()

# Try to find headers
find_path(CRYPTOPP_INCLUDE_DIRS
  NAMES "cryptopp/cryptlib.h"
  DOC "CryptoPP C header"
)
if (CRYPTOPP_INCLUDE_DIRS)
  message(STATUS "Found CryptoPP include path: \"${CRYPTOPP_INCLUDE_DIRS}\"")
else()
  message(STATUS "Could not find CryptoPP include path")
endif()

if (CRYPTOPP_LIBRARIES AND CRYPTOPP_INCLUDE_DIRS)
  cmake_push_check_state()
  set(CMAKE_REQUIRED_INCLUDES "${CRYPTOPP_INCLUDE_DIRS}")
  check_cxx_source_compiles("
    #include <cryptopp/config.h>
    #include <cstdint>
    int main() {
      static_assert(sizeof(CryptoPP::byte) == sizeof(std::uint8_t), \"Invalid byte size of CryptoPP\");
      return 0;
    }"
    HAVE_CRYPTOPP_BYTE
  )
  cmake_pop_check_state()

  if (HAVE_CRYPTOPP_BYTE)
    # Handle QUIET and REQUIRED and check the necessary variables were set and if so
    # set ``CRYPTOPP_FOUND``
    find_package_handle_standard_args(CRYPTOPP DEFAULT_MSG CRYPTOPP_INCLUDE_DIRS CRYPTOPP_LIBRARIES)
  else()
    message(STATUS "Please use at least CryptoPP version 6.0")
  endif()
endif()
