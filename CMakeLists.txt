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

add_subdirectory(lib)
add_subdirectory(bin)

export(TARGETS libpor libutil
	FILE libpor-targets.cmake
)
