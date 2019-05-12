#include "PorEventManager.h"
#include "../CoreStats.h"
#include "../Executor.h"

#include "por/configuration.h"

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

#include <string>

using namespace klee;

namespace {
  llvm::cl::opt<bool>
  LogPorEvents("log-por-events",
               llvm::cl::init(false));

  enum class StandbyStatePolicy { Minimal, Half, Third, All };

  llvm::cl::opt<StandbyStatePolicy> StandbyStates(
    "standby-states",
    llvm::cl::desc("Specify the standby state policy"),
    llvm::cl::values(
        clEnumValN(
          StandbyStatePolicy::Minimal, "minimal",
          "Only record standby states for thread_init of the main thread."),
        clEnumValN(
          StandbyStatePolicy::Half, "half",
          "Only record standby states for at most every second event (per configuration)."),
        clEnumValN(
          StandbyStatePolicy::Third, "third",
          "Only record standby states for at most every third event (per configuration)."),
        clEnumValN(
          StandbyStatePolicy::All, "all",
          "Record standby states for all events (default).")
        KLEE_LLVM_CL_VAL_END),
    llvm::cl::init(StandbyStatePolicy::All));
}

std::string PorEventManager::getNameOfEvent(por_event_t kind) {
  switch (kind) {
    case por_local: return "local";
    case por_program_init: return "program_init";

    case por_thread_create: return "thread_create";
    case por_thread_init: return "thread_init";
    case por_thread_join: return "thread_join";
    case por_thread_exit: return "thread_exit";

    case por_lock_create: return "lock_create";
    case por_lock_destroy: return "lock_destroy";
    case por_lock_release: return "lock_release";
    case por_lock_acquire: return "lock_acquire";

    case por_condition_variable_create: return "condition_variable_create";
    case por_condition_variable_destroy: return "condition_variable_destroy";
    case por_signal: return "signal";
    case por_broadcast: return "broadcast";
    case por_wait1: return "wait1";
    case por_wait2: return "wait2";

    default: return "undefined";
  }
}

void PorEventManager::logEventThreadAndKind(const ExecutionState &state, por_event_t kind) {
  llvm::errs() << "POR event: " << getNameOfEvent(kind) << " with current thread " << state.currentThreadId();
}

void PorEventManager::checkIfCatchUpIsNeeded(ExecutionState &state) {
  if (state.porConfiguration->needs_catch_up()) {
    // make sure we do not miss any events in case a different
    // thread needs to be scheduled after catching up to this event
    state.needsThreadScheduling = true;
  }
}

void PorEventManager::registerStandbyState(ExecutionState &state, por_event_t kind) {
  assert(state.porConfiguration != nullptr);

  bool registerStandbyState = true;
  if (StandbyStates != StandbyStatePolicy::All) {
    registerStandbyState = (kind == por_thread_init && state.currentThreadId() == 1);
    if (!registerStandbyState && StandbyStates != StandbyStatePolicy::Minimal) {
      auto dist = state.porConfiguration->distance_to_last_standby_state(&state);
      if (StandbyStates == StandbyStatePolicy::Half) {
        registerStandbyState = (dist >= 2);
      } else if (StandbyStates == StandbyStatePolicy::Third) {
        registerStandbyState = (dist >= 3);
      }
    }
  }

  if (registerStandbyState) {
    ExecutionState *newState = new ExecutionState(state);
    executor.standbyStates.push_back(newState);
    newState->porConfiguration = std::make_unique<por::configuration>(*state.porConfiguration);
    state.porConfiguration->standby_execution_state(newState);
    ++stats::standbyStates;
  }
}


bool PorEventManager::registerLocal(ExecutionState &state, std::vector<bool> path) {
  if (LogPorEvents) {
    logEventThreadAndKind(state, por_local);

    llvm::errs() << "\n";
  }

  state.porConfiguration->local(state.currentThreadId(), std::move(path));

  checkIfCatchUpIsNeeded(state);
  registerStandbyState(state, por_local);

  return true;
}

bool PorEventManager::registerThreadCreate(ExecutionState &state, const ThreadId &tid) {
  assert(state.currentThreadId() != tid);
  if (LogPorEvents) {
    logEventThreadAndKind(state, por_thread_create);

    llvm::errs() << " and created thread " << tid << "\n";
  }

  state.porConfiguration->spawn_thread(state.currentThreadId(), tid);

  checkIfCatchUpIsNeeded(state);
  registerStandbyState(state, por_thread_create);
  return true;
}

bool PorEventManager::registerThreadInit(ExecutionState &state, const ThreadId &tid) {
  if (LogPorEvents) {
    logEventThreadAndKind(state, por_thread_init);

    llvm::errs() << " and initialized thread " << tid << "\n";
  }

  if (tid == ThreadId(1)) {
    // main thread only: event already present in configuration
    checkIfCatchUpIsNeeded(state);
    registerStandbyState(state, por_thread_init);
    return true;
  }

  assert(0 && "thread_init can only be registered as part of thread_create");
  return false;
}

bool PorEventManager::registerThreadExit(ExecutionState &state, const ThreadId &tid) {
  if (LogPorEvents) {
    logEventThreadAndKind(state, por_thread_exit);

    llvm::errs() << " and exited thread " << tid << "\n";
  }
  
  state.porConfiguration->exit_thread(tid);

  checkIfCatchUpIsNeeded(state);
  registerStandbyState(state, por_thread_exit);
  return true;
}

bool PorEventManager::registerThreadJoin(ExecutionState &state, const ThreadId &joinedThread) {
  if (LogPorEvents) {
    logEventThreadAndKind(state, por_thread_join);

    llvm::errs() << " and joined thread " << joinedThread << "\n";
  }
  
  state.porConfiguration->join_thread(state.currentThreadId(), joinedThread);

  checkIfCatchUpIsNeeded(state);
  registerStandbyState(state, por_thread_join);
  return true;
}


bool PorEventManager::registerLockCreate(ExecutionState &state, std::uint64_t mId) {
  if (LogPorEvents) {
    logEventThreadAndKind(state, por_lock_create);

    llvm::errs() << " on mutex " << mId << "\n";
  }
  
  state.porConfiguration->create_lock(state.currentThreadId(), mId);

  checkIfCatchUpIsNeeded(state);
  registerStandbyState(state, por_lock_create);
  return true;
}

bool PorEventManager::registerLockDestroy(ExecutionState &state, std::uint64_t mId) {
  if (LogPorEvents) {
    logEventThreadAndKind(state, por_lock_destroy);

    llvm::errs() << " on mutex " << mId << "\n";
  }

  state.porConfiguration->destroy_lock(state.currentThreadId(), mId);

  checkIfCatchUpIsNeeded(state);
  registerStandbyState(state, por_lock_destroy);
  return true;
}

bool PorEventManager::registerLockAcquire(ExecutionState &state, std::uint64_t mId) {
  if (LogPorEvents) {
    logEventThreadAndKind(state, por_lock_acquire);

    llvm::errs() << " on mutex " << mId << "\n";
  }

  state.porConfiguration->acquire_lock(state.currentThreadId(), mId);

  checkIfCatchUpIsNeeded(state);
  registerStandbyState(state, por_lock_acquire);
  return true;
}

bool PorEventManager::registerLockRelease(ExecutionState &state, std::uint64_t mId) {
  if (LogPorEvents) {
    logEventThreadAndKind(state, por_lock_release);

    llvm::errs() << " on mutex " << mId << "\n";
  }

  state.porConfiguration->release_lock(state.currentThreadId(), mId);

  checkIfCatchUpIsNeeded(state);
  registerStandbyState(state, por_lock_release);
  return true;
}


bool PorEventManager::registerCondVarCreate(ExecutionState &state, std::uint64_t cId) {
  if (LogPorEvents) {
    logEventThreadAndKind(state, por_condition_variable_create);

    llvm::errs() << " on cond. var " << cId << "\n";
  }

  state.porConfiguration->create_cond(state.currentThreadId(), cId);

  checkIfCatchUpIsNeeded(state);
  registerStandbyState(state, por_condition_variable_create);
  return true;
}

bool PorEventManager::registerCondVarDestroy(ExecutionState &state, std::uint64_t cId) {
  if (LogPorEvents) {
    logEventThreadAndKind(state, por_condition_variable_destroy);

    llvm::errs() << " on cond. var " << cId << "\n";
  }

  state.porConfiguration->destroy_cond(state.currentThreadId(), cId);

  checkIfCatchUpIsNeeded(state);
  registerStandbyState(state, por_condition_variable_destory);
  return true;
}

bool PorEventManager::registerCondVarSignal(ExecutionState &state, std::uint64_t cId, const ThreadId& notifiedThread) {
  if (LogPorEvents) {
    logEventThreadAndKind(state, por_signal);

    llvm::errs() << " on cond. var " << cId << " and signalled thread " << notifiedThread << "\n";
  }

  state.porConfiguration->signal_thread(state.currentThreadId(), cId, notifiedThread);

  checkIfCatchUpIsNeeded(state);
  registerStandbyState(state, por_signal);
  return true;
}

bool PorEventManager::registerCondVarBroadcast(ExecutionState &state, std::uint64_t cId,
                                               const std::vector<ThreadId> &threads) {
  if (LogPorEvents) {
    logEventThreadAndKind(state, por_broadcast);

    llvm::errs() << " on cond. var " << cId << " and broadcasted threads:";
    for (const auto& tid : threads) {
      llvm::errs() << " " << tid;
    }
    llvm::errs() << "\n";
  }

  state.porConfiguration->broadcast_threads(state.currentThreadId(), cId, threads);

  checkIfCatchUpIsNeeded(state);
  registerStandbyState(state, por_broadcast);
  return true;
}

bool PorEventManager::registerCondVarWait1(ExecutionState &state, std::uint64_t cId, std::uint64_t mId) {
  if (LogPorEvents) {
    logEventThreadAndKind(state, por_wait1);

    llvm::errs() << " on cond. var " << cId << " and mutex " << mId << "\n";
  }

  state.porConfiguration->wait1(state.currentThreadId(), cId, mId);

  checkIfCatchUpIsNeeded(state);
  registerStandbyState(state, por_wait1);
  return true;
}

bool PorEventManager::registerCondVarWait2(ExecutionState &state, std::uint64_t cId, std::uint64_t mId) {
  if (LogPorEvents) {
    logEventThreadAndKind(state, por_wait2);

    llvm::errs() << " on cond. var " << cId << " and mutex " << mId << "\n";
  }

  state.porConfiguration->wait2(state.currentThreadId(), cId, mId);

  checkIfCatchUpIsNeeded(state);
  registerStandbyState(state, por_wait2);
  return true;
}
