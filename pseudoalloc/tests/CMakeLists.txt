add_custom_target(check COMMAND ctest --output-on-failure)

file(GLOB_RECURSE test_files CONFIGURE_DEPENDS "src/*.cpp")
foreach(file ${test_files})
	get_filename_component(fileName ${file} NAME_WLE)
	file(RELATIVE_PATH fileRelative ${PROJECT_SOURCE_DIR} ${file})

	add_executable(${fileName} ${file})
	target_link_libraries(${fileName} libpseudoalloc)
	set_target_properties(${fileName} PROPERTIES COMPILE_FLAGS "-pedantic -Wall -Wextra -Werror ${CMAKE_CXX_FLAGS_ASAN} -U NDEBUG -DPSEUDOALLOC_CHECKED -DUSE_PSEUDOALLOC")
	set_target_properties(${fileName} PROPERTIES LINK_FLAGS ${CMAKE_EXE_LINKER_FLAGS_ASAN})

	add_test(NAME ${fileRelative} COMMAND ${fileName})
	add_dependencies(check ${fileName})
endforeach()
