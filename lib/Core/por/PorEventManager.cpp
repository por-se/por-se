#include <string>

#include "llvm/Support/CommandLine.h"

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
    printf("Por event: %s with current thread %lu\n",
           getNameOfEvent(kind).c_str(), state.currentThreadId());
  }

  return true;
}
