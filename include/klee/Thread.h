#ifndef KLEE_THREAD_H
#define KLEE_THREAD_H

#include "klee/Expr.h"
#include "klee/Internal/Module/Cell.h"
#include "klee/Internal/Module/KInstIterator.h"
#include "klee/Internal/Module/KModule.h"

// FIXME: We do not want to be exposing these? :(
#include "../../lib/Core/CallPathManager.h"
// #include "CallPathManager.h"

#include <vector>

namespace klee {
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

    StackFrame(KInstIterator caller, KFunction *kf);
    StackFrame(const StackFrame &s);
    ~StackFrame();
  };

  // Threads do only store their own stack
  // the actual memory will always be saved in the ExecutionState
  class Thread {
    friend class Executor;
    friend class ExecutionState;
    friend class StatsTracker;
    friend class Searcher;
    friend class WeightedRandomSearcher;
    friend class SpecialFunctionHandler;

    public:
      // Copied from ExecutionState.h
      typedef std::vector<StackFrame> stack_ty;

      /// @brief Type for all thread ids
      typedef uint64_t ThreadId;

    private:
      struct MemoryAccess {
        uint8_t type;
        ref<Expr> offset;
        uint64_t epoch;

        MemoryAccess(uint8_t type, ref<Expr> offset, uint64_t epoch);
        MemoryAccess(const MemoryAccess &a);
      };

    public:
      enum ThreadState {
        PREEMPTED = 0,
        SLEEPING = 1,
        RUNNABLE = 2,
        EXITED = 3,
      };

      static const uint8_t READ_ACCESS = 1;
      static const uint8_t WRITE_ACCESS = 2;
      static const uint8_t FREE_ACCESS = 4;
      static const uint8_t ALLOC_ACCESS = 8;

    private:
      /// @brief Pointer to instruction to be executed after the current
      /// instruction
      KInstIterator pc;

      /// @brief Pointer to instruction which is currently executed
      KInstIterator prevPc;

      /// @brief Stack representing the current instruction stream
      stack_ty stack;

      /// @brief thread id that should be unique for the program
      ThreadId tid;

      /// @brief Remember from which Basic Block control flow arrived
      /// (i.e. to select the right phi values)
      unsigned incomingBBIndex;

      /// @brief the current sync point this thread is at
      uint64_t synchronizationPoint;

      /// @brief the state this thread is in
      ThreadState state;

      /// @brief memory accesses this thread has done during the current phase
      std::map<const MemoryObject*, std::vector<MemoryAccess>> syncPhaseAccesses;

      /// @brief map of syncs between threads
      std::map<ThreadId, uint64_t> threadSyncs;

    public:
      Thread(ThreadId tid, KFunction* threadStartRoutine);
      Thread(const Thread &s);
      ThreadId getThreadId();

    private:
      void popStackFrame();
      void pushFrame(KInstIterator caller, KFunction *kf);

      bool trackMemoryAccess(const MemoryObject* target, ref<Expr> offset, uint8_t type);
  };
}

#endif //KLEE_THREAD_H
