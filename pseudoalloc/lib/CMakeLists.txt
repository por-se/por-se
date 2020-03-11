add_library(libpseudoalloc INTERFACE)

# Copied from the klee source code
if (EXISTS "${CMAKE_SOURCE_DIR}/.git")
	# Get information from git. We use third-party code to do this. The nice
	# thing about this code is it will trigger a re-configure if the HEAD changes
	# which means when we build the simulator, it should always have the correct
	# git information.
	include(${CMAKE_SOURCE_DIR}/cmake/GetGitRevisionDescription.cmake)
	get_git_head_revision(_NOT_USED_PSEUDOALLOC_GIT_REFSPEC PSEUDOALLOC_GIT_SHA1HASH)
	message(STATUS "PSEUDOALLOC_GIT_SHA1HASH: ${PSEUDOALLOC_GIT_SHA1HASH}")
else()
	set(PSEUDOALLOC_GIT_SHA1HASH "unknown")
endif()
set(AUTO_GEN_MSG "AUTOMATICALLY GENERATED. DO NOT EDIT!")
configure_file(${CMAKE_CURRENT_LIST_DIR}/include/pseudoalloc/version.h.in ${CMAKE_CURRENT_BINARY_DIR}/include/pseudoalloc/version.h)

target_sources(libpseudoalloc INTERFACE
	"${CMAKE_CURRENT_LIST_DIR}/include/pseudoalloc/pseudoalloc.h"

	"${CMAKE_CURRENT_BINARY_DIR}/include/pseudoalloc/version.h"
)

target_include_directories(libpseudoalloc INTERFACE "${CMAKE_CURRENT_LIST_DIR}/include")
target_include_directories(libpseudoalloc INTERFACE "${CMAKE_CURRENT_BINARY_DIR}/include")
