# Copied from the klee source code
if (EXISTS "${CMAKE_SOURCE_DIR}/.git")
	# Get information from git. We use third-party code to do this. The nice
	# thing about this code is it will trigger a re-configure if the HEAD changes
	# which means when we build the simulator, it should always have the correct
	# git information.
	include(${CMAKE_SOURCE_DIR}/cmake/GetGitRevisionDescription.cmake)
	get_git_head_revision(_NOT_USED_POR_GIT_REFSPEC POR_GIT_SHA1HASH)
	message(STATUS "POR_GIT_SHA1HASH: ${POR_GIT_SHA1HASH}")
else()
	set(POR_GIT_SHA1HASH "unknown")
endif()
set(AUTO_GEN_MSG "AUTOMATICALLY GENERATED. DO NOT EDIT!")
configure_file(${CMAKE_CURRENT_LIST_DIR}/version.h.in ${CMAKE_CURRENT_BINARY_DIR}/include/por/version.h)

target_sources(libpor PUBLIC
	"${CMAKE_CURRENT_LIST_DIR}/event/base.h"
	"${CMAKE_CURRENT_LIST_DIR}/event/broadcast.h"
	"${CMAKE_CURRENT_LIST_DIR}/event/condition_variable_create.h"
	"${CMAKE_CURRENT_LIST_DIR}/event/condition_variable_destroy.h"
	"${CMAKE_CURRENT_LIST_DIR}/event/event.h"
	"${CMAKE_CURRENT_LIST_DIR}/event/local.h"
	"${CMAKE_CURRENT_LIST_DIR}/event/lock_acquire.h"
	"${CMAKE_CURRENT_LIST_DIR}/event/lock_create.h"
	"${CMAKE_CURRENT_LIST_DIR}/event/lock_destroy.h"
	"${CMAKE_CURRENT_LIST_DIR}/event/lock_release.h"
	"${CMAKE_CURRENT_LIST_DIR}/event/program_init.h"
	"${CMAKE_CURRENT_LIST_DIR}/event/signal.h"
	"${CMAKE_CURRENT_LIST_DIR}/event/thread_create.h"
	"${CMAKE_CURRENT_LIST_DIR}/event/thread_exit.h"
	"${CMAKE_CURRENT_LIST_DIR}/event/thread_init.h"
	"${CMAKE_CURRENT_LIST_DIR}/event/thread_join.h"
	"${CMAKE_CURRENT_LIST_DIR}/event/wait1.h"
	"${CMAKE_CURRENT_LIST_DIR}/event/wait2.h"

	"${CMAKE_CURRENT_LIST_DIR}/comb.h"
	"${CMAKE_CURRENT_LIST_DIR}/cone.h"
	"${CMAKE_CURRENT_LIST_DIR}/configuration.h"
	"${CMAKE_CURRENT_LIST_DIR}/csd.h"
	"${CMAKE_CURRENT_LIST_DIR}/erv.h"
	"${CMAKE_CURRENT_LIST_DIR}/node.h"
	"${CMAKE_CURRENT_LIST_DIR}/thread_id.h"
	"${CMAKE_CURRENT_LIST_DIR}/unfolding.h"

	"${CMAKE_CURRENT_BINARY_DIR}/include/por/version.h"
)

target_include_directories(libpor PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/include")
