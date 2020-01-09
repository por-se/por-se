#ifndef KLEE_POREVENTMANAGER_H
#define KLEE_POREVENTMANAGER_H

#include "klee/Thread.h"
#include "klee/por/events.h"

#include "por/node.h"

namespace por {
  class node;
  namespace event {
    class event;
  }
}

namespace klee {
  class ExecutionState;

  class PorEventManager {
    std::map<klee::MemoryFingerprintValue, const por::event::event *> fingerprints;

    public:
      bool registerLocal(ExecutionState &, const std::vector<ExecutionState *> &, bool snapshotsAllowed = true);

    private:
      static std::string getNameOfEvent(por_event_t kind);
      bool shouldRegisterStandbyState(const ExecutionState &state, por_event_t kind);
      std::shared_ptr<const ExecutionState> createStandbyState(const ExecutionState &state, por_event_t kind);
      void logEventThreadAndKind(const ExecutionState &state, por_event_t kind);
      void extendPorNode(ExecutionState&, std::function<por::node::registration_t(por::configuration&)>&&);

    public:
      bool registerThreadCreate(ExecutionState &state, const ThreadId &tid);
      bool registerThreadExit(ExecutionState &state, const ThreadId &tid, bool atomic);
      bool registerThreadJoin(ExecutionState &state, const ThreadId &joinedThread);
      bool registerThreadInit(ExecutionState &state, const ThreadId &tid);

      bool registerLockCreate(ExecutionState &state, std::uint64_t mId);
      bool registerLockDestroy(ExecutionState &state, std::uint64_t mId);
      bool registerLockAcquire(ExecutionState &state, std::uint64_t mId, bool snapshotsAllowed = true);
      bool registerLockRelease(ExecutionState &state, std::uint64_t mId, bool snapshotsAllowed, bool atomic);

      bool registerCondVarCreate(ExecutionState &state, std::uint64_t cId);
      bool registerCondVarDestroy(ExecutionState &state, std::uint64_t cId);
      bool registerCondVarSignal(ExecutionState &state, std::uint64_t cId, const ThreadId &notifiedThread);
      bool registerCondVarBroadcast(ExecutionState &state, std::uint64_t cId, const std::vector<ThreadId> &threads);
      bool registerCondVarWait1(ExecutionState &state, std::uint64_t cId, std::uint64_t mId);
      bool registerCondVarWait2(ExecutionState &state, std::uint64_t cId, std::uint64_t mId);

      void attachFingerprintToEvent(ExecutionState &state, const por::event::event &event);
      void findNewCutoff(ExecutionState &state);
  };
};

#endif /* KLEE_POREVENTMANAGER_H */
