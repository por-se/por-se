add_library(libpor)
target_link_libraries(libpor PUBLIC libutil)

include("${CMAKE_CURRENT_LIST_DIR}/include/CMakeLists.txt")

target_sources(libpor PRIVATE
	"${CMAKE_CURRENT_LIST_DIR}/event/base.cpp"
	"${CMAKE_CURRENT_LIST_DIR}/cone.cpp"
	"${CMAKE_CURRENT_LIST_DIR}/configuration.cpp"
	"${CMAKE_CURRENT_LIST_DIR}/comb.cpp"
	"${CMAKE_CURRENT_LIST_DIR}/csd.cpp"
	"${CMAKE_CURRENT_LIST_DIR}/erv.cpp"
	"${CMAKE_CURRENT_LIST_DIR}/node.cpp"
	"${CMAKE_CURRENT_LIST_DIR}/unfolding.cpp"
)

add_klee_target_options(libpor)
