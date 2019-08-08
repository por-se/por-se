file(GLOB_RECURSE test_files CONFIGURE_DEPENDS "src/*.cpp")

foreach(file ${test_files})
	get_filename_component(fileName ${file} NAME_WLE)
	file(RELATIVE_PATH fileRelative ${PROJECT_SOURCE_DIR} ${file})

	add_executable(${fileName} ${file})
	target_link_libraries(${fileName} libpor)
	set_source_files_properties(${file} PROPERTIES COMPILE_FLAGS -g)

	# Hack to let cmake actually build the test files first everytime before compiling them
	add_test(NAME "build-${fileName}" COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}" --config $<CONFIG> --target ${fileName})

	# Actually test
	add_test(NAME ${fileRelative} COMMAND ${fileName})
	set_tests_properties(${fileRelative} PROPERTIES DEPENDS ${fileName})

	# Adding the "compile" dependency
	set_tests_properties(${fileRelative} PROPERTIES FIXTURES_REQUIRED "fixture-${fileName}")
	set_tests_properties("build-${fileName}" PROPERTIES FIXTURES_SETUP "fixture-${fileName}")
endforeach()