#ifndef KLEE_PORCMDLINE_H
#define KLEE_PORCMDLINE_H

#include "klee/OptionCategories.h"

#include "llvm/Support/CommandLine.h"

namespace klee {

inline llvm::cl::opt<bool> EnableCutoffEvents("cutoff-events",
                                              llvm::cl::desc("Enable state pruning via cutoff events (default=on)"),
                                              llvm::cl::init(true),
                                              llvm::cl::cat(MultithreadingCat));

inline llvm::cl::opt<bool> DebugCutoffEvents("debug-cutoff-events",
                                             llvm::cl::desc("Log information about fingerprinting and cutoff events to stderr (default=off)"),
                                             llvm::cl::init(false),
                                             llvm::cl::cat(DebugCat));

inline llvm::cl::opt<std::size_t> MaxContextSwitchDegree(
  "max-csd",
  llvm::cl::desc("Only explore alternatives with context swtch degree up to this limit.  Set to 0 to disable (default=10)"),
  llvm::cl::init(10),
  llvm::cl::cat(MultithreadingCat));

}
#endif // KLEE_PORCMDLINE_H
