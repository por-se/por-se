#if !defined(KLEE_CALLPRINTER_H)
#define KLEE_CALLPRINTER_H

#include "klee/Internal/Module/KModule.h"
#include "klee/Internal/Module/Cell.h"
#include "klee/Expr/Expr.h"

#include "klee/Thread.h"

namespace klee {
  class CallPrinter {
    public:
      static void printCall(llvm::raw_ostream& os, llvm::Function* f, const std::vector<ref<Expr>>& args);
      static void printCall(llvm::raw_ostream& os, KFunction* kf, const StackFrame& sf);

      static void printCallReturn(llvm::raw_ostream& os, llvm::Function* f, ref<Expr> value);
  };
};

#endif /* KLEE_CALLPRINTER_H */
