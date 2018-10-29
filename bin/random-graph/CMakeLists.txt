add_executable(random-graph "")
target_link_libraries(random-graph libpor)

include("${CMAKE_CURRENT_LIST_DIR}/src/CMakeLists.txt")