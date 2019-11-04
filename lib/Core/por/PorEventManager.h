#ifndef KLEE_POREVENTMANAGER_H
#define KLEE_POREVENTMANAGER_H

#include "klee/Thread.h"
#include "klee/por/events.h"

namespace por {
  class node;
  namespace event {
    class event;
  }
}

namespace klee {
  class ExecutionState;

  class PorEventManager {
    Executor &executor;
    bool roundRobin = false;

    public:
      PorEventManager() = delete;
      explicit PorEventManager(Executor &executor) : executor(executor) {}

      bool registerLocal(ExecutionState &state, const std::vector<ExecutionState *> &addedStates);

      void enableRoundRobinMode() { roundRobin = true; }

    private:
      static std::string getNameOfEvent(por_event_t kind);
      bool shouldRegisterStandbyState(const ExecutionState &state, por_event_t kind);
      std::shared_ptr<const ExecutionState> createStandbyState(const ExecutionState &state, por_event_t kind);
      void logEventThreadAndKind(const ExecutionState &state, por_event_t kind);
      void checkIfCatchUpIsNeeded(ExecutionState &state);

    public:
      bool registerThreadCreate(ExecutionState &state, const ThreadId &tid);
      bool registerThreadExit(ExecutionState &state, const ThreadId &tid);
      bool registerThreadJoin(ExecutionState &state, const ThreadId &joinedThread);
      bool registerThreadInit(ExecutionState &state, const ThreadId &tid);

      bool registerLockCreate(ExecutionState &state, std::uint64_t mId);
      bool registerLockDestroy(ExecutionState &state, std::uint64_t mId);
      bool registerLockAcquire(ExecutionState &state, std::uint64_t mId, bool snapshotsAllowed = true);
      bool registerLockRelease(ExecutionState &state, std::uint64_t mId);

      bool registerCondVarCreate(ExecutionState &state, std::uint64_t cId);
      bool registerCondVarDestroy(ExecutionState &state, std::uint64_t cId);
      bool registerCondVarSignal(ExecutionState &state, std::uint64_t cId, const ThreadId &notifiedThread);
      bool registerCondVarBroadcast(ExecutionState &state, std::uint64_t cId, const std::vector<ThreadId> &threads);
      bool registerCondVarWait1(ExecutionState &state, std::uint64_t cId, std::uint64_t mId);
      bool registerCondVarWait2(ExecutionState &state, std::uint64_t cId, std::uint64_t mId);

      void attachFingerprintToEvent(ExecutionState &state, const por::event::event &event);
  };
};

#endif /* KLEE_POREVENTMANAGER_H */
