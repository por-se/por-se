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
  CallPathManager.cpp
  Context.cpp
  CoreStats.cpp
  ExecutionState.cpp
  Executor.cpp
  ExecutorUtil.cpp
  ExternalDispatcher.cpp
  GlobalObjectsMap.cpp
  ImpliedValue.cpp
  Memory.cpp
  MemoryManager.cpp
  MemoryState.cpp
  PTree.cpp
  Searcher.cpp
  SeedInfo.cpp
  SpecialFunctionHandler.cpp
  StatsTracker.cpp
  Thread.cpp
  TimingSolver.cpp
  UserSearcher.cpp
  por/PorEventManager.cpp
  RaceDetection/DataRaceDetection.cpp
  RaceDetection/ObjectAccesses.cpp
  RaceDetection/EpochMemoryAccesses.cpp
)

klee_add_component(kleeCore ${CORE_COMPONENTS})

# TODO: Work out what the correct LLVM components are for
# kleeCore.
set(LLVM_COMPONENTS
  core
  executionengine
  mcjit
  native
  support
)

klee_get_llvm_libs(LLVM_LIBS ${LLVM_COMPONENTS})
target_link_libraries(kleeCore PUBLIC ${LLVM_LIBS} ${SQLITE3_LIBRARIES})
target_link_libraries(kleeCore PRIVATE
  kleeBasic
  kleeFingerprint
  kleeModule
  kleaverSolver
  kleaverExpr
  kleeSupport
  kleePor
)
