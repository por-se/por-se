#ifndef KLEE_STATEPRUNINGFLAGS_H
#define KLEE_STATEPRUNINGFLAGS_H

#include "llvm/Support/CommandLine.h"

namespace klee {

extern llvm::cl::opt<bool> PruneStates;
extern llvm::cl::opt<bool> DebugStatePruning;

}
#endif // KLEE_STATEPRUNINGFLAGS_H
