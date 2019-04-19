#ifndef KLEE_POREVENTMANAGER_H
#define KLEE_POREVENTMANAGER_H

#include "klee/Thread.h"
#include "klee/ExecutionState.h"
#include "klee/por/events.h"

namespace klee {
  class PorEventManager {
    Executor &executor;
    public:
      PorEventManager() = delete;
      PorEventManager(Executor &executor) : executor(executor) {}

      bool registerPorEvent(ExecutionState &state, por_event_t kind, std::vector<std::uint64_t> args);

    private:
      bool registerPorEventInternal(ExecutionState &state, por_event_t kind, std::vector<std::uint64_t> &args);

      static std::string getNameOfEvent(por_event_t kind);

      bool handleThreadCreate(ExecutionState &state, Thread::ThreadId tid);
      bool handleThreadExit(ExecutionState &state, Thread::ThreadId tid);
      bool handleThreadJoin(ExecutionState &state, Thread::ThreadId joinedThread);

      bool handleLockCreate(ExecutionState &state, std::uint64_t mId);
      bool handleLockDestroy(ExecutionState &state, std::uint64_t mId);
      bool handleLockAcquire(ExecutionState &state, std::uint64_t mId);
      bool handleLockRelease(ExecutionState &state, std::uint64_t mId);

      bool handleCondVarCreate(ExecutionState &state, std::uint64_t cId);
      bool handleCondVarDestroy(ExecutionState &state, std::uint64_t cId);
      bool handleCondVarSignal(ExecutionState &state, std::uint64_t cId, Thread::ThreadId notifiedThread);
      bool handleCondVarBroadcast(ExecutionState &state, std::uint64_t cId, std::vector<std::uint64_t> threads);
      bool handleCondVarWait1(ExecutionState &state, std::uint64_t cId, std::uint64_t mId);
      bool handleCondVarWait2(ExecutionState &state, std::uint64_t cId, std::uint64_t mId);
  };
};

#endif //KLEE_POREVENTMANAGER_H
