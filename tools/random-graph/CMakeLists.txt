#===------------------------------------------------------------------------===#
#
#                     The KLEE Symbolic Virtual Machine
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
#===------------------------------------------------------------------------===#
add_executable(random-graph
  main.cpp
)

target_link_libraries(random-graph kleePor)

install(TARGETS klee RUNTIME DESTINATION bin)
