#ifndef KLEE_THREAD_H
#define KLEE_THREAD_H

#include "ThreadId.h"

#include "klee/Expr/Expr.h"
#include "klee/Fingerprint/MemoryFingerprint.h"
#include "klee/Internal/Module/Cell.h"
#include "klee/Internal/Module/KInstIterator.h"
#include "klee/Internal/Module/KModule.h"

#include "pseudoalloc/pseudoalloc.h"
#include "por/event/event.h"

#include <optional>
#include <variant>
#include <vector>
#include <unordered_map>

namespace por {
  namespace event {
    template<typename D>
    class local;
  }
}

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

  enum class ThreadState : std::uint8_t {
    Waiting,
    Runnable,
    Exited,
    Cutoff
  };

  // Threads do only store their own stack
  // the actual memory will always be saved in the ExecutionState
  class Thread {
    friend class Executor;
    friend class ExecutionState;
    friend class MemoryState;
    friend class MemoryManager;
    friend class SpecialFunctionHandler;

    public:
      typedef std::vector<StackFrame> stack_ty;

      struct decision_t {
        std::uint64_t branch;
        ref<Expr> expr;
      };

      struct wait_none_t { };
      struct wait_lock_t { por::event::lock_id_t lock; };
      struct wait_cv_1_t { por::event::cond_id_t cond; por::event::lock_id_t lock; };
      struct wait_cv_2_t { por::event::cond_id_t cond; por::event::lock_id_t lock; };
      struct wait_join_t { ThreadId thread; };

      using waiting_t = std::variant<wait_none_t,wait_lock_t,wait_cv_1_t,wait_cv_2_t,wait_join_t>;

    private:
      /// @brief Pointer to instruction to be executed after the current
      /// instruction
      KInstIterator pc;

      /// @brief Pointer to instruction which is currently executed
      KInstIterator prevPc;

      /// @brief Set of live locals at current stage of execution
      const std::vector<const KInstruction *> *liveSet = nullptr;

      /// @brief Stack representing the current instruction stream
      stack_ty stack;

      /// @brief thread id that should be unique for the program
      ThreadId tid;

      /// @brief Remember from which Basic Block control flow arrived
      /// (i.e. to select the right phi values)
      unsigned incomingBBIndex;

      /// @brief life cycle state of this thread
      ThreadState state = ThreadState::Runnable;

      /// @brief the resource the thread is currently waiting for
      waiting_t waiting = wait_none_t{};

      /// @brief value of the pthread_t pointer the thread was created with
      ref<Expr> runtimeStructPtr;

      /// @brief the errno of the thread
      const MemoryObject* errnoMo;

      /// @brief Contains true / false for each decision since last por_local registration
      std::vector<std::pair<std::uint64_t, ref<Expr>>> pathSincePorLocal;

      /// @brief counts how many threads this thread already created
      std::uint16_t spawnedThreads = 0;

      std::unique_ptr<pseudoalloc::allocator_t> threadHeapAlloc;
      std::unique_ptr<pseudoalloc::stack_allocator_t> threadStackAlloc;

      MemoryFingerprint fingerprint;

    public:
      Thread() = delete;
      Thread(const Thread &thread);
      Thread(ThreadId tid, KFunction *entry);

      using local_event_t = por::event::local<decltype(pathSincePorLocal)::value_type>;

      ThreadId getThreadId() const;

      bool isRunnable(const por::configuration &configuration) const noexcept;

      template<typename W>
      std::optional<W> isWaitingOn() const noexcept {
        if (auto w = std::get_if<W>(&waiting)) {
          return *w;
        }
        return std::nullopt;
      }

    private:
      void popStackFrame();
      void pushFrame(KInstIterator caller, KFunction *kf);

      decision_t getNextDecisionFromLocal(const por::event::event *event) noexcept {
        assert(event->kind() == por::event::event_kind::local);
        auto local = static_cast<const Thread::local_event_t *>(event);
        std::size_t nextIndex = pathSincePorLocal.size();
        assert(local->path().size() > nextIndex);
        auto pair = local->path()[nextIndex];
        return {pair.first, pair.second};
      }

    friend class por::event::local<decltype(pathSincePorLocal)::value_type>;
  };
}

#endif // KLEE_THREAD_H
