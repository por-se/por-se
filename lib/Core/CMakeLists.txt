#===------------------------------------------------------------------------===#
#
#                     The KLEE Symbolic Virtual Machine
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
#===------------------------------------------------------------------------===#
set(CORE_COMPONENTS
  AddressSpace.cpp
  MergeHandler.cpp
  CallPathManager.cpp
  Context.cpp
  CoreStats.cpp
  ExecutionState.cpp
  Executor.cpp
  ExecutorTimers.cpp
  ExecutorUtil.cpp
  ExternalDispatcher.cpp
  ImpliedValue.cpp
  Memory.cpp
  MemoryFingerprint_CryptoPP_BLAKE2b.cpp
  MemoryManager.cpp
  MemoryState.cpp
  PTree.cpp
  MemoryAccessTracker.cpp
  Searcher.cpp
  SeedInfo.cpp
  SpecialFunctionHandler.cpp
  StatsTracker.cpp
  Thread.cpp
  TimingSolver.cpp
  UserSearcher.cpp
)

if (VERIFIED_FINGERPRINTS)
  list(APPEND CORE_COMPONENTS MemoryFingerprint_StringSet.cpp)
endif()

klee_add_component(kleeCore ${CORE_COMPONENTS})

# TODO: Work out what the correct LLVM components are for
# kleeCore.
set(LLVM_COMPONENTS
  core
  support
)

if ("${LLVM_PACKAGE_VERSION}" VERSION_EQUAL "3.6" OR
    "${LLVM_PACKAGE_VERSION}" VERSION_GREATER "3.6")
  list(APPEND LLVM_COMPONENTS mcjit executionengine native)
else()
  list(APPEND LLVM_COMPONENTS jit engine)
endif()

klee_get_llvm_libs(LLVM_LIBS ${LLVM_COMPONENTS})
target_link_libraries(kleeCore PUBLIC ${LLVM_LIBS})
target_link_libraries(kleeCore PRIVATE
  kleeBasic
  kleeModule
  kleaverSolver
  kleaverExpr
  kleeSupport
  libpor
)
