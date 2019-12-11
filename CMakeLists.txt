cmake_minimum_required(VERSION 3.11)

project(simulator
	VERSION 1.0.0
	LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_C_FLAGS_ASAN "-Og -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-optimize-sibling-calls"
	CACHE STRING "Flags used by the C compiler during sanitized builds."
	FORCE)
set(CMAKE_CXX_FLAGS_ASAN "-Og -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-optimize-sibling-calls"
	CACHE STRING "Flags used by the C++ compiler during sanitized builds."
	FORCE)
set(CMAKE_EXE_LINKER_FLAGS_ASAN "-fsanitize=address,undefined"
	CACHE STRING "Flags used for linking binaries during sanitized builds."
	FORCE)
set(CMAKE_SHARED_LINKER_FLAGS_ASAN "-fsanitize=address,undefined"
	CACHE STRING "Flags used by the shared libraries linker during ASan builds."
	FORCE)

option(KLEE_VERIFIED_FINGERPRINTS "Enable verified fingerprints (only with KLEE_INCLUDE_DIR)" OFF)

find_path(KLEE_INCLUDE_DIR klee/klee.h)

if(NOT KLEE_INCLUDE_DIR AND KLEE_VERIFIED_FINGERPRINTS)
	message(FATAL_ERROR "KLEE_VERIFIED_FINGERPRINTS can only be used with KLEE_INCLUDE_DIR!")
endif()

function(ADD_KLEE_TARGET_OPTIONS TARGET)
	if(KLEE_INCLUDE_DIR)
		target_compile_definitions(${TARGET} PUBLIC "LIBPOR_KLEE")
		target_include_directories(${TARGET} PRIVATE ${KLEE_INCLUDE_DIR})
		target_compile_definitions(${TARGET} PRIVATE "KLEE_OUTSIDE_BUILD_TREE")
		if(KLEE_VERIFIED_FINGERPRINTS)
			target_compile_definitions(${TARGET} PUBLIC "ENABLE_VERIFIED_FINGERPRINTS")
		endif()
	endif()
endfunction()

add_subdirectory(lib)
add_subdirectory(bin)

export(TARGETS libpor libutil
	FILE libpor-targets.cmake
)

enable_testing()
add_subdirectory(tests)
