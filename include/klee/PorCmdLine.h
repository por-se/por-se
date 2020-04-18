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
                                             llvm::cl::desc("Log information about cutoff events to stderr (default=off)"),
                                             llvm::cl::init(false),
                                             llvm::cl::cat(DebugCat));

inline llvm::cl::opt<bool> DebugFingerprints("debug-fingerprints",
                                             llvm::cl::desc("Log information about fingerprinting to stderr (default=off)"),
                                             llvm::cl::init(false),
                                             llvm::cl::cat(DebugCat));

inline llvm::cl::opt<std::size_t> MaxContextSwitchDegree(
  "max-csd",
  llvm::cl::desc("Only explore events with context switch degree up to this limit (default=10)"),
  llvm::cl::init(10),
  llvm::cl::cat(MultithreadingCat));

inline llvm::cl::opt<bool> UnlimitedContextSwitchDegree(
  "max-csd-unlimited",
  llvm::cl::desc("Do not limit context switch degree (default=off)"),
  llvm::cl::init(false),
  llvm::cl::cat(MultithreadingCat));

}
#endif // KLEE_PORCMDLINE_H
