#ifndef KLEE_STATEPRUNINGFLAGS_H
#define KLEE_STATEPRUNINGFLAGS_H

#include "llvm/Support/CommandLine.h"

namespace klee {

inline llvm::cl::opt<bool> PruneStates("state-pruning",
                                       llvm::cl::desc("Enable pruning of states (default=off)"),
                                       llvm::cl::init(true));

inline llvm::cl::opt<bool> DebugStatePruning("debug-state-pruning",
                                             llvm::cl::desc("Log state pruning debug info to stderr (default=off)"),
                                             llvm::cl::init(false));

}
#endif // KLEE_STATEPRUNINGFLAGS_H
