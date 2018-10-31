target_sources(libpor INTERFACE
	"${CMAKE_CURRENT_LIST_DIR}/event/base.h"
	"${CMAKE_CURRENT_LIST_DIR}/event/event.h"
	"${CMAKE_CURRENT_LIST_DIR}/event/local.h"
	"${CMAKE_CURRENT_LIST_DIR}/event/lock_acquire.h"
	"${CMAKE_CURRENT_LIST_DIR}/event/lock_create.h"
	"${CMAKE_CURRENT_LIST_DIR}/event/lock_destroy.h"
	"${CMAKE_CURRENT_LIST_DIR}/event/lock_release.h"
	"${CMAKE_CURRENT_LIST_DIR}/event/program_init.h"
	"${CMAKE_CURRENT_LIST_DIR}/event/thread_create.h"
	"${CMAKE_CURRENT_LIST_DIR}/event/thread_init.h"
	"${CMAKE_CURRENT_LIST_DIR}/event/thread_exit.h"
	"${CMAKE_CURRENT_LIST_DIR}/program.h"
)