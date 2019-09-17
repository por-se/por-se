add_executable(sample "")
target_link_libraries(sample libpseudoalloc)
target_sources(sample PRIVATE
	"${CMAKE_CURRENT_LIST_DIR}/sample.cpp"
)

add_executable(allocate "")
target_link_libraries(allocate libpseudoalloc)
target_sources(allocate PRIVATE
	"${CMAKE_CURRENT_LIST_DIR}/allocate.cpp"
)
