#ifndef KLEE_THREAD_H
#define KLEE_THREAD_H

#include "klee/Expr.h"
#include "klee/Internal/Module/Cell.h"
#include "klee/Internal/Module/KInstIterator.h"
#include "klee/Internal/Module/KModule.h"

// FIXME: We do not want to be exposing these? :(
#include "../../lib/Core/MemoryFingerprint.h"

#include <vector>
#include <unordered_map>

namespace klee {
  class Array;
  class CallPathNode;

  struct StackFrame {
    KInstIterator caller;
    KFunction *kf;
    CallPathNode *callPathNode;

    std::vector<const MemoryObject *> allocas;
    Cell *locals;

    /// Minimum distance to an uncovered instruction once the function
    /// returns. This is not a good place for this but is used to
    /// quickly compute the context sensitive minimum distance to an
    /// uncovered instruction. This value is updated by the StatsTracker
    /// periodically.
    uint64_t minDistToUncoveredOnReturn;

    // For vararg functions: arguments not passed via parameter are
    // stored (packed tightly) in a local (alloca) memory object. This
    // is set up to match the way the front-end generates vaarg code (it
    // does not pass vaarg through as expected). VACopy is lowered inside
    // of intrinsic lowering.
    MemoryObject *varargs;

    // changes w.r.t. this stack frame
    MemoryFingerprintDelta fingerprintDelta;

    StackFrame(KInstIterator caller, KFunction *kf);
    StackFrame(const StackFrame &s);
    ~StackFrame();
  };

  // Threads do only store their own stack
  // the actual memory will always be saved in the ExecutionState
  class Thread {
    friend class Executor;
    friend class ExecutionState;
    friend class MemoryState;
    friend class StatsTracker;
    friend class Searcher;
    friend class WeightedRandomSearcher;
    friend class SpecialFunctionHandler;

    public:
      typedef std::vector<StackFrame> stack_ty;

      /// @brief Type for all thread ids
      typedef uint64_t ThreadId;

    public:
      enum ThreadState {
        SLEEPING = 0,
        RUNNABLE = 1,
        EXITED = 2,
      };

    private:
      /// @brief Pointer to instruction to be executed after the current
      /// instruction
      KInstIterator pc;

      /// @brief Pointer to instruction which is currently executed
      KInstIterator prevPc;

      /// @brief Pointer to instruction which was last executed
      KInstIterator liveSetPc;

      /// @brief Stack representing the current instruction stream
      stack_ty stack;

      /// @brief thread id that should be unique for the program
      ThreadId tid;

      /// @brief Remember from which Basic Block control flow arrived
      /// (i.e. to select the right phi values)
      unsigned incomingBBIndex;

      /// @brief the state this thread is in
      ThreadState state;

      /// @brief the argument with which the thread was started
      ref<Expr> startArg;

      /// @brief the errno of the thread
      // Note: only has a valid reference if the thread is currently not scheduled
      ref<Expr> threadErrno;

      /// @brief if the thread scheduling was disabled when this thread was going sleeping
      bool threadSchedulingWasDisabled;

    public:
      Thread(ThreadId tid, KFunction* threadStartRoutine);
      Thread(const Thread &s);

      ThreadId getThreadId() const;
      ref<Expr> getStartArgument() const;

    private:
      void popStackFrame();
      void pushFrame(KInstIterator caller, KFunction *kf);
  };
}

#endif //KLEE_THREAD_H
