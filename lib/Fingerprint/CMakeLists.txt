#===------------------------------------------------------------------------===#
#
#                     The KLEE Symbolic Virtual Machine
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
#===------------------------------------------------------------------------===#

set(FINGERPRINT_COMPONENTS
  MemoryFingerprint_CryptoPP_BLAKE2b.cpp
)

if (VERIFIED_FINGERPRINTS)
  list(APPEND FINGERPRINT_COMPONENTS MemoryFingerprint_StringSet.cpp)
endif()

klee_add_component(kleeFingerprint ${FINGERPRINT_COMPONENTS})

set(LLVM_COMPONENTS
  support
)
klee_get_llvm_libs(LLVM_LIBS ${LLVM_COMPONENTS})
target_link_libraries(kleeFingerprint PUBLIC ${LLVM_LIBS})
target_link_libraries(kleeFingerprint PRIVATE
  kleeBasic
  kleeModule
  kleaverExpr
  kleeSupport
)
