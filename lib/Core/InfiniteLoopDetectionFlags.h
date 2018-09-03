#ifndef KLEE_INFINITELOOPDETECTIONFLAGS_H
#define KLEE_INFINITELOOPDETECTIONFLAGS_H

#include "klee/CommandLine.h"

namespace klee {

extern llvm::cl::opt<bool> DetectInfiniteLoops;

enum DebugInfiniteLoopDetectionType {
  STDERR_STATE,
  STDERR_TRACE
};

extern llvm::cl::bits<DebugInfiniteLoopDetectionType> DebugInfiniteLoopDetection;

extern llvm::cl::opt<bool> InfiniteLoopDetectionDisableTwoPredecessorOpt;

extern llvm::cl::opt<bool> InfiniteLoopDetectionDisableLiveVariableAnalysis;

extern llvm::cl::opt<bool> InfiniteLoopDetectionDebugLiveVariableAnalysis;

extern llvm::cl::opt<bool> InfiniteLoopLogStateJSON;

#ifdef HAVE_ZLIB_H
extern llvm::cl::opt<bool> InfiniteLoopCompressLogStateJSON;
#endif

}
#endif
