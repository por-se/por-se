#include "PorEventManager.h"
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
}

std::string PorEventManager::getNameOfEvent(por_event_t kind) {
  switch (kind) {
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

bool PorEventManager::registerPorEvent(ExecutionState &state, por_event_t kind, std::vector<std::uint64_t> args) {
  if (LogPorEvents) {
    llvm::errs() << "POR event: " << getNameOfEvent(kind) << " with current thread " << state.currentThreadId() << " and args: ";

    for (const auto& arg : args) {
      llvm::errs() << arg  << " ";
    }

    llvm::errs() << "\n";
  }

  // Make sure that we always have the correct number of arguments
  if (kind == por_wait1 || kind == por_wait2) {
    if (args.size() != 2) {
      return false;
    }
  } else if (kind == por_signal || kind == por_broadcast) {
    if (args.empty()) {
      return false;
    }
  } else {
    // Every other event type requires exactly one parameter
    if (args.size() != 1) {
      return false;
    }
  }

  if (registerPorEventInternal(state, kind, args)) {
    ExecutionState *newState = new ExecutionState(state);
    newState->porConfiguration = std::make_unique<por::configuration>(*state.porConfiguration);
    executor.standbyStates.push_back(newState);

    state.porConfiguration->standby_execution_state(newState);
    return true;
  }

  return false;
}

bool PorEventManager::registerPorEventInternal(ExecutionState &state, por_event_t kind, std::vector<std::uint64_t> &args) {
  switch (kind) {
    case por_thread_create:
      return handleThreadCreate(state, static_cast<Thread::ThreadId>(args[0]));

    case por_thread_init: {
      auto tid = static_cast<Thread::ThreadId>(args[0]);
      if (tid == 1) {
        // main thread only: event already present in configuration
        return true;
      }
      assert(0 && "thread_init can only be registered as part of thread_create");
      return false;
    }

    case por_thread_join:
      return handleThreadJoin(state, static_cast<Thread::ThreadId>(args[0]));

    case por_thread_exit:
      return handleThreadExit(state, static_cast<Thread::ThreadId>(args[0]));

    case por_lock_create:
      return handleLockCreate(state, args[0]);

    case por_lock_destroy:
      return handleLockDestroy(state, args[0]);

    case por_lock_release:
      return handleLockRelease(state, args[0]);

    case por_lock_acquire:
      return handleLockAcquire(state, args[0]);

    case por_condition_variable_create:
      return handleCondVarCreate(state, args[0]);

    case por_condition_variable_destroy:
      return handleCondVarDestroy(state, args[0]);

    case por_signal:
      return handleCondVarSignal(state, args[0], static_cast<Thread::ThreadId>(args[1]));

    case por_broadcast:
      return handleCondVarBroadcast(state, args[0], { args.begin() + 1, args.end() });

    case por_wait1:
      return handleCondVarWait1(state, args[0], args[1]);

    case por_wait2:
      return handleCondVarWait2(state, args[0], args[1]);

    default:
      return false;
  }
}


bool PorEventManager::handleThreadCreate(ExecutionState &state, Thread::ThreadId tid) {
  assert(state.currentThreadId() != tid);
  state.porConfiguration->spawn_thread(state.currentThreadId(), tid);
  return true;
}

bool PorEventManager::handleThreadExit(ExecutionState &state, Thread::ThreadId tid) {
  state.porConfiguration->exit_thread(tid);
  return true;
}

bool PorEventManager::handleThreadJoin(ExecutionState &state, Thread::ThreadId joinedThread) {
  state.porConfiguration->join_thread(state.currentThreadId(), joinedThread);
  return true;
}


bool PorEventManager::handleLockCreate(ExecutionState &state, std::uint64_t mId) {
  state.porConfiguration->create_lock(state.currentThreadId(), mId);
  return true;
}

bool PorEventManager::handleLockDestroy(ExecutionState &state, std::uint64_t mId) {
  state.porConfiguration->destroy_lock(state.currentThreadId(), mId);
  return true;
}

bool PorEventManager::handleLockAcquire(ExecutionState &state, std::uint64_t mId) {
  state.porConfiguration->acquire_lock(state.currentThreadId(), mId);
  return true;
}

bool PorEventManager::handleLockRelease(ExecutionState &state, std::uint64_t mId) {
  state.porConfiguration->release_lock(state.currentThreadId(), mId);
  return true;
}


bool PorEventManager::handleCondVarCreate(ExecutionState &state, std::uint64_t cId) {
  state.porConfiguration->create_cond(state.currentThreadId(), cId);
  return true;
}

bool PorEventManager::handleCondVarDestroy(ExecutionState &state, std::uint64_t cId) {
  state.porConfiguration->destroy_cond(state.currentThreadId(), cId);
  return true;
}

bool PorEventManager::handleCondVarSignal(ExecutionState &state, std::uint64_t cId, Thread::ThreadId notifiedThread) {
  state.porConfiguration->signal_thread(state.currentThreadId(), cId, notifiedThread);
  return true;
}

bool PorEventManager::handleCondVarBroadcast(ExecutionState &state, std::uint64_t cId, std::vector<Thread::ThreadId> threads) {
  state.porConfiguration->broadcast_threads(state.currentThreadId(), cId, threads);
  return true;
}

bool PorEventManager::handleCondVarWait1(ExecutionState &state, std::uint64_t cId, std::uint64_t mId) {
  state.porConfiguration->wait1(state.currentThreadId(), cId, mId);
  return true;
}

bool PorEventManager::handleCondVarWait2(ExecutionState &state, std::uint64_t cId, std::uint64_t mId) {
  state.porConfiguration->wait2(state.currentThreadId(), cId, mId);
  return true;
}
