#===------------------------------------------------------------------------===#
#
#                     The KLEE Symbolic Virtual Machine
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
#===------------------------------------------------------------------------===#

klee_add_component(kleePor
  cone.cpp
  configuration.cpp
  comb.cpp
  csd.cpp
  erv.cpp
  event.cpp
  node.cpp
  unfolding.cpp
)

target_link_libraries(kleePor PRIVATE
  kleeCore
  kleeFingerprint
)
