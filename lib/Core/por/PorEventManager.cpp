#include "PorEventManager.h"
#include "../CoreStats.h"
#include "../Executor.h"

#include "por/configuration.h"
#include "por/event/event.h"
#include "por/node.h"

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

#include <string>
#include <utility>

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
          "Only record standby states for thread_init of the main thread and any condition_variable_create."),
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

  void extendPorNode(ExecutionState& state, std::function<por::node::registration_t(por::configuration&)>&& callback) {
    if(state.porNode->needs_catch_up()) {
      state.porNode->catch_up(std::move(callback));
      return;
    }

    state.porNode = state.porNode->make_left_child(std::move(callback));
  }
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
  if (state.porNode->needs_catch_up() || roundRobin) {
    // make sure we do not miss any events in case a different
    // thread needs to be scheduled after catching up to this event
    state.needsThreadScheduling = true;
  }
}

bool PorEventManager::shouldRegisterStandbyState(const ExecutionState &state, por_event_t kind) {
  bool result = true;
  if (StandbyStates != StandbyStatePolicy::All) {
    result = (kind == por_thread_init && state.isOnMainThread()) || (kind == por_condition_variable_create);

    if (!result && StandbyStates != StandbyStatePolicy::Minimal) {
      auto dist = state.porNode->distance_to_last_standby_state();
      if (StandbyStates == StandbyStatePolicy::Half) {
        result = (dist >= 2);
      } else if (StandbyStates == StandbyStatePolicy::Third) {
        result = (dist >= 3);
      }
    }
  }
  return result && (state.threads.size() > 1 || kind == por_thread_init);
}

std::shared_ptr<const ExecutionState> PorEventManager::createStandbyState(const ExecutionState &s, por_event_t kind) {
  if(shouldRegisterStandbyState(s, kind)) {
    auto standby = std::make_shared<const ExecutionState>(s);
    ++stats::standbyStates;
    return standby;
  }
  return nullptr;
}


bool PorEventManager::registerLocal(ExecutionState &state, const std::vector<ExecutionState *> &addedStates) {
  if (LogPorEvents) {
    logEventThreadAndKind(state, por_local);

    llvm::errs() << " and path ";
    for(auto b : state.currentThread().pathSincePorLocal) {
      llvm::errs() << b << " ";
    }

    llvm::errs() << "\n";
  }

  assert(!state.currentThread().pathSincePorLocal.empty());
  assert(std::find(addedStates.begin(), addedStates.end(), &state) == addedStates.end());

  checkIfCatchUpIsNeeded(state);

  if(state.porNode->needs_catch_up()) {
    assert(addedStates.empty());

    state.porNode->catch_up([this, &state](por::configuration& cfg) {
      auto& thread = state.currentThread();
      std::vector<std::uint64_t> path = std::move(thread.pathSincePorLocal);
      thread.pathSincePorLocal = {};
      por::event::event const* e = cfg.local(thread.getThreadId(), std::move(path));
      auto standby = createStandbyState(state, por_local);
      return std::make_pair(e, std::move(standby));
    });

    return true;
  }

  por::node *n = state.porNode;
  state.porNode = state.porNode->make_left_child([this, &state](por::configuration& cfg) {
    auto& thread = state.currentThread();
    std::vector<std::uint64_t> path = std::move(thread.pathSincePorLocal);
    thread.pathSincePorLocal = {};
    por::event::event const* e = cfg.local(thread.getThreadId(), std::move(path));
    auto standby = createStandbyState(state, por_local);
    return std::make_pair(e, std::move(standby));
  });

  assert(state.porNode->parent() == n);

  for(auto& s : addedStates) {
    if(!s->currentThread().pathSincePorLocal.empty()) {
      s->porNode = n->make_right_local_child([this, &s](por::configuration& cfg) {
        auto& thread = s->currentThread();
        std::vector<std::uint64_t> path = std::move(thread.pathSincePorLocal);
        thread.pathSincePorLocal = {};
        por::event::event const* e = cfg.local(thread.getThreadId(), std::move(path));
        auto standby = createStandbyState(*s, por_local);
        return std::make_pair(e, std::move(standby));
      });
      n = s->porNode->parent();
    }
  }

  return true;
}

bool PorEventManager::registerThreadCreate(ExecutionState &state, const ThreadId &tid) {
  assert(state.currentThreadId() != tid);
  if (LogPorEvents) {
    logEventThreadAndKind(state, por_thread_create);

    llvm::errs() << " and created thread " << tid << "\n";
  }

  checkIfCatchUpIsNeeded(state);

  extendPorNode(state, [this, &state, &tid](por::configuration& cfg) {
    por::event::event const* e = cfg.create_thread(state.currentThreadId(), tid);
    auto standby = createStandbyState(state, por_thread_create);
    attachFingerprintToEvent(state, *e);
    return std::make_pair(e, standby);
  });

  return true;
}

bool PorEventManager::registerThreadInit(ExecutionState &state, const ThreadId &tid) {
  if (LogPorEvents) {
    logEventThreadAndKind(state, por_thread_init);

    llvm::errs() << " and initialized thread " << tid << "\n";
  }

  checkIfCatchUpIsNeeded(state);

  if (tid == ExecutionState::mainThreadId) {
    // event already present in configuration
    state.porNode = state.porNode->make_left_child([this, &tid, &state](por::configuration& cfg) {
      auto it = cfg.thread_heads().find(tid);
      assert(it != cfg.thread_heads().end());
      por::event::event const* e = it->second;
      auto standby = createStandbyState(state, por_thread_init);
      return std::make_pair(e, std::move(standby));
    });
  } else {
    extendPorNode(state, [this, &state, &tid](por::configuration& cfg) {
      por::event::event const* e = cfg.init_thread(tid);
      auto standby = createStandbyState(state, por_thread_init);
      return std::make_pair(e, standby);
    });
  }

  return true;
}

bool PorEventManager::registerThreadExit(ExecutionState &state, const ThreadId &tid) {
  if (LogPorEvents) {
    logEventThreadAndKind(state, por_thread_exit);

    llvm::errs() << " and exited thread " << tid << "\n";
  }

  checkIfCatchUpIsNeeded(state);

  extendPorNode(state, [this, &state, &tid](por::configuration& cfg) {
    por::event::event const* e = cfg.exit_thread(tid);
    auto standby = createStandbyState(state, por_thread_exit);
    attachFingerprintToEvent(state, *e);
    return std::make_pair(e, std::move(standby));
  });

  return true;
}

bool PorEventManager::registerThreadJoin(ExecutionState &state, const ThreadId &joinedThread) {
  if (LogPorEvents) {
    logEventThreadAndKind(state, por_thread_join);

    llvm::errs() << " and joined thread " << joinedThread << "\n";
  }

  checkIfCatchUpIsNeeded(state);

  extendPorNode(state, [this, &state, &joinedThread](por::configuration& cfg) {
    por::event::event const* e = cfg.join_thread(state.currentThreadId(), joinedThread);
    auto standby = createStandbyState(state, por_thread_join);
    attachFingerprintToEvent(state, *e);
    return std::make_pair(e, std::move(standby));
  });

  return true;
}


bool PorEventManager::registerLockCreate(ExecutionState &state, std::uint64_t mId) {
  if (LogPorEvents) {
    logEventThreadAndKind(state, por_lock_create);

    llvm::errs() << " on mutex " << mId << "\n";
  }

  checkIfCatchUpIsNeeded(state);

  extendPorNode(state, [this, &state, &mId](por::configuration& cfg) {
    por::event::event const* e = cfg.create_lock(state.currentThreadId(), mId);
    auto standby = createStandbyState(state, por_lock_create);
    attachFingerprintToEvent(state, *e);
    return std::make_pair(e, std::move(standby));
  });

  return true;
}

bool PorEventManager::registerLockDestroy(ExecutionState &state, std::uint64_t mId) {
  if (LogPorEvents) {
    logEventThreadAndKind(state, por_lock_destroy);

    llvm::errs() << " on mutex " << mId << "\n";
  }

  checkIfCatchUpIsNeeded(state);

  extendPorNode(state, [this, &state, &mId](por::configuration& cfg) {
    por::event::event const* e = cfg.destroy_lock(state.currentThreadId(), mId);
    auto standby = createStandbyState(state, por_lock_destroy);
    attachFingerprintToEvent(state, *e);
    return std::make_pair(e, std::move(standby));
  });

  return true;
}

bool PorEventManager::registerLockAcquire(ExecutionState &state, std::uint64_t mId, bool snapshotsAllowed) {
  if (LogPorEvents) {
    logEventThreadAndKind(state, por_lock_acquire);

    llvm::errs() << " on mutex " << mId << "\n";
  }

  checkIfCatchUpIsNeeded(state);

  extendPorNode(state, [this, &state, &mId, &snapshotsAllowed](por::configuration& cfg) -> por::node::registration_t {
    por::event::event const* e = cfg.acquire_lock(state.currentThreadId(), mId);

    if(snapshotsAllowed) {
      auto standby = createStandbyState(state, por_lock_acquire);
      attachFingerprintToEvent(state, *e);
      return std::make_pair(e, std::move(standby));
    }
    attachFingerprintToEvent(state, *e);
    return std::make_pair(e, nullptr);
  });

  return true;
}

bool PorEventManager::registerLockRelease(ExecutionState &state, std::uint64_t mId) {
  if (LogPorEvents) {
    logEventThreadAndKind(state, por_lock_release);

    llvm::errs() << " on mutex " << mId << "\n";
  }

  checkIfCatchUpIsNeeded(state);

  extendPorNode(state, [this, &state, &mId](por::configuration& cfg) {
    por::event::event const* e = cfg.release_lock(state.currentThreadId(), mId);
    auto standby = createStandbyState(state, por_lock_release);
    attachFingerprintToEvent(state, *e);
    return std::make_pair(e, std::move(standby));
  });

  return true;
}


bool PorEventManager::registerCondVarCreate(ExecutionState &state, std::uint64_t cId) {
  if (LogPorEvents) {
    logEventThreadAndKind(state, por_condition_variable_create);

    llvm::errs() << " on cond. var " << cId << "\n";
  }

  checkIfCatchUpIsNeeded(state);

  extendPorNode(state, [this, &state, &cId](por::configuration& cfg) {
    por::event::event const* e = cfg.create_cond(state.currentThreadId(), cId);
    auto standby = createStandbyState(state, por_condition_variable_create);
    attachFingerprintToEvent(state, *e);
    return std::make_pair(e, std::move(standby));
  });

  return true;
}

bool PorEventManager::registerCondVarDestroy(ExecutionState &state, std::uint64_t cId) {
  if (LogPorEvents) {
    logEventThreadAndKind(state, por_condition_variable_destroy);

    llvm::errs() << " on cond. var " << cId << "\n";
  }

  checkIfCatchUpIsNeeded(state);

  extendPorNode(state, [this, &state, &cId](por::configuration& cfg) {
    por::event::event const* e = cfg.destroy_cond(state.currentThreadId(), cId);
    auto standby = createStandbyState(state, por_condition_variable_destroy);
    attachFingerprintToEvent(state, *e);
    return std::make_pair(e, std::move(standby));
  });

  return true;
}

bool PorEventManager::registerCondVarSignal(ExecutionState &state, std::uint64_t cId, const ThreadId& notifiedThread) {
  if (LogPorEvents) {
    logEventThreadAndKind(state, por_signal);

    llvm::errs() << " on cond. var " << cId << " and signalled thread " << notifiedThread << "\n";
  }

  checkIfCatchUpIsNeeded(state);

  extendPorNode(state, [this, &state, &cId, &notifiedThread](por::configuration& cfg) {
    por::event::event const* e = cfg.signal_thread(state.currentThreadId(), cId, notifiedThread);
    auto standby = createStandbyState(state, por_signal);
    attachFingerprintToEvent(state, *e);
    return std::make_pair(e, std::move(standby));
  });

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

  checkIfCatchUpIsNeeded(state);

  extendPorNode(state, [this, &state, &cId, &threads](por::configuration& cfg) {
    por::event::event const* e = cfg.broadcast_threads(state.currentThreadId(), cId, threads);
    auto standby = createStandbyState(state, por_broadcast);
    attachFingerprintToEvent(state, *e);
    return std::make_pair(e, std::move(standby));
  });

  return true;
}

bool PorEventManager::registerCondVarWait1(ExecutionState &state, std::uint64_t cId, std::uint64_t mId) {
  if (LogPorEvents) {
    logEventThreadAndKind(state, por_wait1);

    llvm::errs() << " on cond. var " << cId << " and mutex " << mId << "\n";
  }

  checkIfCatchUpIsNeeded(state);

  extendPorNode(state, [this, &state, &cId, &mId](por::configuration& cfg) {
    por::event::event const* e = cfg.wait1(state.currentThreadId(), cId, mId);
    auto standby = createStandbyState(state, por_wait1);
    attachFingerprintToEvent(state, *e);
    return std::make_pair(e, std::move(standby));
  });

  return true;
}

bool PorEventManager::registerCondVarWait2(ExecutionState &state, std::uint64_t cId, std::uint64_t mId) {
  if (LogPorEvents) {
    logEventThreadAndKind(state, por_wait2);

    llvm::errs() << " on cond. var " << cId << " and mutex " << mId << "\n";
  }

  checkIfCatchUpIsNeeded(state);

  extendPorNode(state, [this, &state, &cId, &mId](por::configuration& cfg) {
    por::event::event const* e = cfg.wait2(state.currentThreadId(), cId, mId);
    auto standby = createStandbyState(state, por_wait2);
    attachFingerprintToEvent(state, *e);
    return std::make_pair(e, std::move(standby));
  });

  return true;
}

void PorEventManager::attachFingerprintToEvent(ExecutionState &state, const por::event::event &event) {
  auto thread = state.getThreadById(event.tid());
  assert(thread && "no thread with given id found");

  event._thread_delta = state.memoryState.getThreadDelta(*thread);

  MemoryFingerprint fingerprint;
  fingerprint.addDelta(event._thread_delta);
  for (auto &[tid, c] : event.cone()) {
    fingerprint.addDelta(c->_thread_delta);
  }

  std::vector<ref<Expr>> expressions;
  for (auto expr : state.constraints) {
    expressions.push_back(expr);
  }

  event._fingerprint = fingerprint.getFingerprint(expressions);
}
