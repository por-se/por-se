#include <string>

#include "llvm/Support/CommandLine.h"

#include "por/configuration.h"

#include "PorEventManager.h"

using namespace klee;
using namespace llvm;

namespace {
  cl::opt<bool>
  LogPorEvents("log-por-events",
               cl::init(false));
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
    printf("Por event: %s with current thread %lu and args: %lu\n",
           getNameOfEvent(kind).c_str(), state.currentThreadId(), args[0]);
  }

  // Make sure that we always have the correct number of arguments
  if ((kind == por_wait1 || kind == por_wait2) && args.size() != 2) {
    return false;
  }

  if (kind != por_wait1 && kind != por_wait2 && args.size() != 1) {
    return false;
  }

  switch (kind) {
    case por_thread_create:
      return handleThreadCreate(state, args[0]);

    case por_thread_join:
      return handleThreadJoin(state, args[0]);

    case por_thread_exit:
      return handleThreadExit(state, args[0]);

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
      return handleCondVarSignal(state, args[0]);

    case por_broadcast:
      return handleCondVarBroadcast(state, args[0]);

    case por_wait1:
      return handleCondVarWait1(state, args[0], args[1]);

    case por_wait2:
      return handleCondVarWait2(state, args[0], args[1]);

    default:
      return false;
  }
}


bool PorEventManager::handleThreadCreate(ExecutionState &state, Thread::ThreadId tid) {
  state.porConfiguration->spawn_thread(state.currentThreadId() + 1, tid + 1);
  return true;
}

bool PorEventManager::handleThreadExit(ExecutionState &state, Thread::ThreadId tid) {
  state.porConfiguration->exit_thread(tid + 1);
  return true;
}

bool PorEventManager::handleThreadJoin(ExecutionState &state, Thread::ThreadId joinedThread) {
  state.porConfiguration->join_thread(state.currentThreadId() + 1, joinedThread + 1);
  return true;
}


bool PorEventManager::handleLockCreate(ExecutionState &state, std::uint64_t mId) {
  state.porConfiguration->create_lock(state.currentThreadId() + 1, mId);
  return true;
}

bool PorEventManager::handleLockDestroy(ExecutionState &state, std::uint64_t mId) {
  state.porConfiguration->destroy_lock(state.currentThreadId() + 1, mId);
  return true;
}

bool PorEventManager::handleLockAcquire(ExecutionState &state, std::uint64_t mId) {
  state.porConfiguration->acquire_lock(state.currentThreadId() + 1, mId);
  return true;
}

bool PorEventManager::handleLockRelease(ExecutionState &state, std::uint64_t mId) {
  state.porConfiguration->release_lock(state.currentThreadId() + 1, mId);
  return true;
}


bool PorEventManager::handleCondVarCreate(ExecutionState &state, std::uint64_t cId) {
  state.porConfiguration->create_condition_variable(state.currentThreadId() + 1, cId);
  return true;
}

bool PorEventManager::handleCondVarDestroy(ExecutionState &state, std::uint64_t cId) {
  state.porConfiguration->destroy_condition_variable(state.currentThreadId() + 1, cId);
  return true;
}

bool PorEventManager::handleCondVarSignal(ExecutionState &state, std::uint64_t cId) {
  state.porConfiguration->signal(state.currentThreadId() + 1, cId);
  return true;
}

bool PorEventManager::handleCondVarBroadcast(ExecutionState &state, std::uint64_t cId) {
  state.porConfiguration->broadcast(state.currentThreadId() + 1, cId);
  return true;
}

bool PorEventManager::handleCondVarWait1(ExecutionState &state, std::uint64_t cId, std::uint64_t mId) {
  return true;
}

bool PorEventManager::handleCondVarWait2(ExecutionState &state, std::uint64_t cId, std::uint64_t mId) {
  return true;
}