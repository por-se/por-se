# Tries to find an install of the CryptoPP library and header files
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
  NAMES cryptlib.h
  # For distributions that keep the header files in a `cryptopp` folder
  PATH_SUFFIXES cryptopp
  DOC "CryptoPP header"
)
if (CRYPTOPP_INCLUDE_DIRS)
  message(STATUS "Found CryptoPP include path: \"${CRYPTOPP_INCLUDE_DIRS}\"")
else()
  message(STATUS "Could not find CryptoPP include path")
endif()

# TODO: We should check we can link some simple code against libz3

# Handle QUIET and REQUIRED and check the necessary variables were set and if so
# set ``CRYPTOPP_FOUND``
find_package_handle_standard_args(CRYPTOPP DEFAULT_MSG CRYPTOPP_INCLUDE_DIRS CRYPTOPP_LIBRARIES)
