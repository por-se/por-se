add_library(libpor INTERFACE)
target_link_libraries(libpor INTERFACE libutil)

include("${CMAKE_CURRENT_LIST_DIR}/include/CMakeLists.txt")