#if !defined(KLEE_DEBUGPRINTER_H)
#define KLEE_DEBUGPRINTER_H

#include "klee/Internal/Module/KModule.h"
#include "klee/Internal/Module/Cell.h"
#include "klee/Expr/Expr.h"

#include "klee/Thread.h"

namespace klee {
  class DebugPrinter {
    public:
      static void printCall(llvm::raw_ostream& os, llvm::Function* f, const std::vector<ref<Expr>>& args);
      static void printCall(llvm::raw_ostream& os, KFunction* kf, const StackFrame& sf);

      static void printCallReturn(llvm::raw_ostream& os, llvm::Function* f, ref<Expr> value);

      static void printStateContext(llvm::raw_ostream& os, const ExecutionState& state, bool withFrameIndex = false);
  };
};

#endif /* KLEE_DEBUGPRINTER_H */
