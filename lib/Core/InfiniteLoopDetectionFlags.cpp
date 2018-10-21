#include "InfiniteLoopDetectionFlags.h"

namespace klee {

llvm::cl::opt<bool> DetectInfiniteLoops(
  "detect-infinite-loops",
  llvm::cl::desc("Enable detection of infinite loops (default=false)"),
  llvm::cl::init(true)); // XXX: prototype same-state detection

llvm::cl::bits<DebugInfiniteLoopDetectionType> DebugInfiniteLoopDetection(
  "debug-infinite-loop-detection",
  llvm::cl::desc("Log information about Infinite Loop Detection."),
  llvm::cl::values(
    clEnumValN(STDERR_STATE, "state:stderr",
      "Log all MemoryState information to stderr"),
    clEnumValN(STDERR_TRACE, "trace:stderr",
      "Log all MemoryTrace information to stderr")
        KLEE_LLVM_CL_VAL_END),
  llvm::cl::CommaSeparated);

llvm::cl::opt<bool> InfiniteLoopDetectionDisableTwoPredecessorOpt(
  "infinite-loop-detection-disable-two-predecessor-optimization",
  llvm::cl::desc("Disable infinite loop detection optimization that only starts searching for loops on basic blocks with at least two predecessors (default=false)"),
  llvm::cl::init(true)); // XXX: prototype same-state detection

llvm::cl::opt<bool> InfiniteLoopDetectionDisableLiveVariableAnalysis(
  "infinite-loop-detection-disable-live-variable-analysis",
  llvm::cl::desc("Disable live variable analysis used for infinite loop detection (default=false)"),
  llvm::cl::init(false));

llvm::cl::opt<bool> InfiniteLoopLogStateJSON(
  "infinite-loop-detection-log-state-json-files",
  llvm::cl::desc("Creates two files (states.json, states_fork.json) in output directory that record relevant information about states such as MemoryTrace length (default=false)"),
  llvm::cl::init(false));

#ifdef HAVE_ZLIB_H
llvm::cl::opt<bool> InfiniteLoopCompressLogStateJSON(
  "infinite-loop-detection-compress-log-state-json-files",
  llvm::cl::desc("Compress the files created by -infinite-loop-detection-log-state-json-files in gzip format."),
  llvm::cl::init(false));
#endif

}
