#include "PorEventManager.h"
#include "../CoreStats.h"

#include "klee/Config/config.h"
#include "klee/ExecutionState.h"
#include "klee/Internal/Support/ErrorHandling.h"
#include "klee/OptionCategories.h"
#include "klee/StatePruningCmdLine.h"

#include "por/configuration.h"
#include "por/event/event.h"
#include "por/node.h"
#include "por/erv.h"

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

#include <string>
#include <utility>

using namespace klee;

namespace {
  llvm::cl::opt<bool>
  DebugEventRegistration("debug-event-registration",
                         llvm::cl::init(false),
                         llvm::cl::cat(DebugCat));

  llvm::cl::opt<bool>
  UseAdequateOrder("use-adequate-order",
                   llvm::cl::desc("Use adequate total order [ERV02] for determining cutoff events (default=true)"),
                   llvm::cl::init(true));

  llvm::cl::opt<unsigned>
  StandbyStates("standby-states",
    llvm::cl::desc("Controls the number of standby states created, use n to attach one to every nth exploration node. "
      "Use 0 for only one standby state and 1 to create a standby state for all nodes possible.  (default=1)"),
    llvm::cl::init(1));
}

std::string PorEventManager::getNameOfEvent(por::event::event_kind kind) {
  switch (kind) {
    case por::event::event_kind::local: return "local";
    case por::event::event_kind::program_init: return "program_init";

    case por::event::event_kind::thread_create: return "thread_create";
    case por::event::event_kind::thread_init: return "thread_init";
    case por::event::event_kind::thread_join: return "thread_join";
    case por::event::event_kind::thread_exit: return "thread_exit";

    case por::event::event_kind::lock_create: return "lock_create";
    case por::event::event_kind::lock_destroy: return "lock_destroy";
    case por::event::event_kind::lock_release: return "lock_release";
    case por::event::event_kind::lock_acquire: return "lock_acquire";

    case por::event::event_kind::condition_variable_create: return "condition_variable_create";
    case por::event::event_kind::condition_variable_destroy: return "condition_variable_destroy";
    case por::event::event_kind::signal: return "signal";
    case por::event::event_kind::broadcast: return "broadcast";
    case por::event::event_kind::wait1: return "wait1";
    case por::event::event_kind::wait2: return "wait2";

    default: return "undefined";
  }
}

bool PorEventManager::extendPorNode(ExecutionState& state, std::function<por::node::registration_t(por::configuration&)>&& callback) {
  if (state.needsCatchUp()) {
    state.porNode = state.porNode->catch_up(std::move(callback), state.peekCatchUp());
    if (state.porNode == nullptr) {
      return false;
    }
    bool success = attachFingerprintToEvent(state, *state.peekCatchUp());
    state.catchUp.pop_front();
    return success;
  }

  auto oldNode = state.porNode;
  state.porNode = state.porNode->make_left_child(std::move(callback));
  if (attachFingerprintToEvent(state, *oldNode->event())) {
    findNewCutoff(state);
    return true;
  }
  return false;
}

void PorEventManager::logEventThreadAndKind(const ExecutionState &state, por::event::event_kind kind) {
  llvm::errs() << "[state id: " << state.id << "] ";
  llvm::errs() << "registering " << getNameOfEvent(kind) << " with current thread " << state.tid();
}

bool PorEventManager::shouldRegisterStandbyState(const ExecutionState &state, por::event::event_kind kind) {
  bool result = (state.threads.size() == 1 && kind == por::event::event_kind::thread_init);
  if (StandbyStates == 0) {
    return result;
  } else if (StandbyStates == 1) {
    return true;
  }
  auto dist = state.porNode->distance_to_last_standby_state();
  return result || (dist >= StandbyStates);
}

std::shared_ptr<const ExecutionState> PorEventManager::createStandbyState(const ExecutionState &s, por::event::event_kind kind) {
  if (shouldRegisterStandbyState(s, kind)) {
    auto standby = std::make_shared<const ExecutionState>(s);
    ++stats::standbyStates;
    return standby;
  }
  return nullptr;
}


bool PorEventManager::registerLocal(ExecutionState &state,
                                    const std::vector<ExecutionState *> &addedStates,
                                    bool snapshotsAllowed) {
  if (DebugEventRegistration) {
    logEventThreadAndKind(state, por::event::event_kind::local);

    llvm::errs() << " and path ";
    for (auto &[b, _] : state.unregisteredDecisions()) {
      llvm::errs() << b << " ";
    }

    llvm::errs() << "\n";
  }

  assert(state.threadState() != ThreadState::Waiting);
  assert(state.hasUnregisteredDecisions());
  assert(std::find(addedStates.begin(), addedStates.end(), &state) == addedStates.end());

  state.needsThreadScheduling = true;

  bool success;

  if (state.needsCatchUp()) {
    assert(addedStates.empty());
    state.porNode = state.porNode->catch_up(
    [this, &state, &snapshotsAllowed, &success](por::configuration& cfg) -> por::node::registration_t {
      auto path = std::move(state.unregisteredDecisions());
      state.unregisteredDecisions() = {};
      por::event::event const* e = cfg.local(state.tid(), std::move(path));
      success = attachFingerprintToEvent(state, *e);
      if (snapshotsAllowed) {
        auto standby = createStandbyState(state, por::event::event_kind::local);
        return std::make_pair(e, std::move(standby));
      }
      return std::make_pair(e, nullptr);
    }, state.peekCatchUp());
    if (state.porNode == nullptr) {
      return false;
    }
    state.catchUp.pop_front();

    return success;
  }

  por::node *n = state.porNode;
  state.porNode = state.porNode->make_left_child(
      [this, &state, &snapshotsAllowed, &success](por::configuration& cfg) -> por::node::registration_t {
    auto path = std::move(state.unregisteredDecisions());
    state.unregisteredDecisions() = {};
    por::event::event const* e = cfg.local(state.tid(), std::move(path));
    success = attachFingerprintToEvent(state, *e);
    if (snapshotsAllowed) {
      auto standby = createStandbyState(state, por::event::event_kind::local);
      return std::make_pair(e, std::move(standby));
    }
    return std::make_pair(e, nullptr);
  });
  findNewCutoff(state);

  assert(state.porNode->parent() == n);

  for (auto& s : addedStates) {
    if (!success) {
      return false;
    }
    if (s->hasUnregisteredDecisions()) {
      s->porNode = n; // needed for distance calculation in shouldRegisterStandbyState
      s->porNode = n->make_right_local_child(
          [this, &s, &snapshotsAllowed, &success](por::configuration& cfg) -> por::node::registration_t {
        auto path = std::move(s->unregisteredDecisions());
        s->unregisteredDecisions() = {};
        por::event::event const* e = cfg.local(s->tid(), std::move(path));
        success = attachFingerprintToEvent(*s, *e);
        if (snapshotsAllowed) {
          auto standby = createStandbyState(*s, por::event::event_kind::local);
          return std::make_pair(e, std::move(standby));
        }
        return std::make_pair(e, nullptr);
      });
      n = s->porNode->parent();
    }
  }

  return success;
}

bool PorEventManager::registerThreadCreate(ExecutionState &state, const ThreadId &tid) {
  assert(state.tid() != tid);
  if (DebugEventRegistration) {
    logEventThreadAndKind(state, por::event::event_kind::thread_create);

    llvm::errs() << " and created thread " << tid << "\n";
  }

  assert(!state.hasUnregisteredDecisions());

  state.needsThreadScheduling = true;

  return extendPorNode(state, [this, &state, &tid](por::configuration& cfg) {
    por::event::event const* e = cfg.create_thread(state.tid(), tid);
    return std::make_pair(e, nullptr);
  });
}

bool PorEventManager::registerThreadInit(ExecutionState &state, const ThreadId &tid) {
  if (DebugEventRegistration) {
    logEventThreadAndKind(state, por::event::event_kind::thread_init);

    llvm::errs() << " and initialized thread " << tid << "\n";
  }

  assert(!state.hasUnregisteredDecisions());

  state.needsThreadScheduling = true;

  bool success;
  if (tid == ExecutionState::mainThreadId) {
    // event already present in configuration
    state.porNode = state.porNode->make_left_child([this, &tid, &state, &success](por::configuration& cfg) {
      auto it = cfg.thread_heads().find(tid);
      assert(it != cfg.thread_heads().end());
      por::event::event const* e = it->second;
      auto standby = createStandbyState(state, por::event::event_kind::thread_init);
      success = attachFingerprintToEvent(state, *e);
      return std::make_pair(e, std::move(standby));
    });
  } else {
    assert(state.tid() != tid);
    success = extendPorNode(state, [this, &state, &tid](por::configuration& cfg) {
      por::event::event const* e = cfg.init_thread(tid, state.tid());
      auto standby = createStandbyState(state, por::event::event_kind::thread_init);
      return std::make_pair(e, standby);
    });
  }

  return success;
}

bool PorEventManager::registerThreadExit(ExecutionState &state, const ThreadId &tid, bool atomic) {
  if (DebugEventRegistration) {
    logEventThreadAndKind(state, por::event::event_kind::thread_exit);

    if (atomic) {
      llvm::errs() << " (atomic)";
    }

    llvm::errs() << " and exited thread " << tid << "\n";
  }

  [[maybe_unused]] auto pred = state.porNode->last_included_event();
  if (atomic) {
    assert(state.porNode->distance_to_last_standby_state() > 0);
  }

  assert(!state.hasUnregisteredDecisions());

  state.needsThreadScheduling = true;

  bool success = extendPorNode(state,
  [this, &state, &tid, &atomic](por::configuration& cfg) -> por::node::registration_t {
    por::event::event const* e = cfg.exit_thread(tid, atomic);

    auto standby = createStandbyState(state, por::event::event_kind::thread_exit);
    return std::make_pair(e, std::move(standby));
  });

  if (atomic) {
    assert(pred == state.porNode->last_included_event()->thread_predecessor());
  }

  return success;
}

bool PorEventManager::registerThreadJoin(ExecutionState &state, const ThreadId &joinedThread) {
  if (DebugEventRegistration) {
    logEventThreadAndKind(state, por::event::event_kind::thread_join);

    llvm::errs() << " and joined thread " << joinedThread << "\n";
  }

  assert(!state.hasUnregisteredDecisions());

  state.needsThreadScheduling = true;

  return extendPorNode(state, [this, &state, &joinedThread](por::configuration& cfg) {
    por::event::event const* e = cfg.join_thread(state.tid(), joinedThread);
    auto standby = createStandbyState(state, por::event::event_kind::thread_join);
    return std::make_pair(e, std::move(standby));
  });
}


bool PorEventManager::registerLockCreate(ExecutionState &state, std::uint64_t mId) {
  if (DebugEventRegistration) {
    logEventThreadAndKind(state, por::event::event_kind::lock_create);

    llvm::errs() << " on mutex " << mId << "\n";
  }

  assert(!state.hasUnregisteredDecisions());

  state.needsThreadScheduling = true;

  return extendPorNode(state, [this, &state, &mId](por::configuration& cfg) {
    por::event::event const* e = cfg.create_lock(state.tid(), mId);
    auto standby = createStandbyState(state, por::event::event_kind::lock_create);
    return std::make_pair(e, std::move(standby));
  });
}

bool PorEventManager::registerLockDestroy(ExecutionState &state, std::uint64_t mId) {
  if (DebugEventRegistration) {
    logEventThreadAndKind(state, por::event::event_kind::lock_destroy);

    llvm::errs() << " on mutex " << mId << "\n";
  }

  assert(!state.hasUnregisteredDecisions());

  state.needsThreadScheduling = true;

  return extendPorNode(state, [this, &state, &mId](por::configuration& cfg) {
    por::event::event const* e = cfg.destroy_lock(state.tid(), mId);
    auto standby = createStandbyState(state, por::event::event_kind::lock_destroy);
    return std::make_pair(e, std::move(standby));
  });
}

bool PorEventManager::registerLockAcquire(ExecutionState &state, std::uint64_t mId, bool snapshotsAllowed) {
  if (DebugEventRegistration) {
    logEventThreadAndKind(state, por::event::event_kind::lock_acquire);

    llvm::errs() << " on mutex " << mId << "\n";
  }

  state.needsThreadScheduling = true;

  return extendPorNode(state,
  [this, &state, &mId, &snapshotsAllowed](por::configuration& cfg) -> por::node::registration_t {
    por::event::event const* e = cfg.acquire_lock(state.tid(), mId);
    if (snapshotsAllowed) {
      auto standby = createStandbyState(state, por::event::event_kind::lock_acquire);
      return std::make_pair(e, std::move(standby));
    }
    return std::make_pair(e, nullptr);
  });
}

bool PorEventManager::registerLockRelease(ExecutionState &state, std::uint64_t mId, bool snapshot, bool atomic) {
  if (DebugEventRegistration) {
    logEventThreadAndKind(state, por::event::event_kind::lock_release);

    if (atomic) {
      llvm::errs() << " (atomic)";
    }

    llvm::errs() << " on mutex " << mId << "\n";
  }

  [[maybe_unused]] auto pred = state.porNode->last_included_event();
  if (atomic) {
    assert(state.porNode->distance_to_last_standby_state() > 0);
  }

  assert(!state.hasUnregisteredDecisions());

  state.needsThreadScheduling = true;

  bool success = extendPorNode(state,
  [this, &state, &mId, &snapshot, &atomic](por::configuration& cfg) -> por::node::registration_t {
    por::event::event const* e = cfg.release_lock(state.tid(), mId, atomic);
    if (snapshot) {
      auto standby = createStandbyState(state, por::event::event_kind::lock_release);
      return std::make_pair(e, std::move(standby));
    }
    return std::make_pair(e, nullptr);
  });

  if (atomic) {
    assert(pred == state.porNode->last_included_event()->thread_predecessor());
  }

  return success;
}


bool PorEventManager::registerCondVarCreate(ExecutionState &state, std::uint64_t cId) {
  if (DebugEventRegistration) {
    logEventThreadAndKind(state, por::event::event_kind::condition_variable_create);

    llvm::errs() << " on cond. var " << cId << "\n";
  }

  assert(!state.hasUnregisteredDecisions());

  state.needsThreadScheduling = true;

  return extendPorNode(state, [this, &state, &cId](por::configuration& cfg) {
    por::event::event const* e = cfg.create_cond(state.tid(), cId);
    auto standby = createStandbyState(state, por::event::event_kind::condition_variable_create);
    return std::make_pair(e, std::move(standby));
  });
}

bool PorEventManager::registerCondVarDestroy(ExecutionState &state, std::uint64_t cId) {
  if (DebugEventRegistration) {
    logEventThreadAndKind(state, por::event::event_kind::condition_variable_destroy);

    llvm::errs() << " on cond. var " << cId << "\n";
  }

  assert(!state.hasUnregisteredDecisions());

  state.needsThreadScheduling = true;

  return extendPorNode(state, [this, &state, &cId](por::configuration& cfg) {
    por::event::event const* e = cfg.destroy_cond(state.tid(), cId);
    auto standby = createStandbyState(state, por::event::event_kind::condition_variable_destroy);
    return std::make_pair(e, std::move(standby));
  });
}

bool PorEventManager::registerCondVarSignal(ExecutionState &state, std::uint64_t cId, const ThreadId& notifiedThread) {
  if (DebugEventRegistration) {
    logEventThreadAndKind(state, por::event::event_kind::signal);

    llvm::errs() << " on cond. var " << cId << " and signalled thread " << notifiedThread << "\n";
  }

  assert(!state.hasUnregisteredDecisions());

  state.needsThreadScheduling = true;

  return extendPorNode(state, [this, &state, &cId, &notifiedThread](por::configuration& cfg) {
    por::event::event const* e = cfg.signal_thread(state.tid(), cId, notifiedThread);
    auto standby = createStandbyState(state, por::event::event_kind::signal);
    return std::make_pair(e, std::move(standby));
  });
}

bool PorEventManager::registerCondVarBroadcast(ExecutionState &state, std::uint64_t cId,
                                               const std::vector<ThreadId> &threads) {
  if (DebugEventRegistration) {
    logEventThreadAndKind(state, por::event::event_kind::broadcast);

    llvm::errs() << " on cond. var " << cId << " and broadcasted threads:";
    for (const auto& tid : threads) {
      llvm::errs() << " " << tid;
    }
    llvm::errs() << "\n";
  }

  assert(!state.hasUnregisteredDecisions());

  state.needsThreadScheduling = true;

  return extendPorNode(state, [this, &state, &cId, &threads](por::configuration& cfg) {
    por::event::event const* e = cfg.broadcast_threads(state.tid(), cId, threads);
    auto standby = createStandbyState(state, por::event::event_kind::broadcast);
    return std::make_pair(e, std::move(standby));
  });
}

bool PorEventManager::registerCondVarWait1(ExecutionState &state, std::uint64_t cId, std::uint64_t mId) {
  if (DebugEventRegistration) {
    logEventThreadAndKind(state, por::event::event_kind::wait1);

    llvm::errs() << " on cond. var " << cId << " and mutex " << mId << "\n";
  }

  assert(!state.hasUnregisteredDecisions());

  state.needsThreadScheduling = true;

  return extendPorNode(state, [this, &state, &cId, &mId](por::configuration& cfg) {
    por::event::event const* e = cfg.wait1(state.tid(), cId, mId);
    auto standby = createStandbyState(state, por::event::event_kind::wait1);
    return std::make_pair(e, std::move(standby));
  });
}

bool PorEventManager::registerCondVarWait2(ExecutionState &state, std::uint64_t cId, std::uint64_t mId) {
  if (DebugEventRegistration) {
    logEventThreadAndKind(state, por::event::event_kind::wait2);

    llvm::errs() << " on cond. var " << cId << " and mutex " << mId << "\n";
  }

  assert(!state.hasUnregisteredDecisions());

  state.needsThreadScheduling = true;

  return extendPorNode(state, [this, &state, &cId, &mId](por::configuration& cfg) {
    por::event::event const* e = cfg.wait2(state.tid(), cId, mId);
    auto standby = createStandbyState(state, por::event::event_kind::wait2);
    return std::make_pair(e, std::move(standby));
  });
}

bool PorEventManager::attachFingerprintToEvent(ExecutionState &state, const por::event::event &event) {
  if (!PruneStates) {
    return true;
  }

  if (DebugEventRegistration) {
    llvm::errs() << "[state id: " << state.id << "] ";
    llvm::errs() << "POR event: " << event.to_string(true) << "\n";
  }

  auto thread = state.getThreadById(event.tid());
  assert(thread && "no thread with given id found");

  MemoryFingerprint copy;
  auto delta = state.memoryState.getThreadDelta(*thread);
  copy.addDelta(delta);

  for (auto &[tid, c] : event.cone()) {
    if (tid != event.tid()) {
      copy.addDelta(c->thread_delta());
    }
  }

  std::vector<ref<Expr>> expressions;
  for (auto &e : event.local_configuration()) {
    if (e->kind() != por::event::event_kind::local) {
      continue;
    }

    auto local = static_cast<const Thread::local_event_t *>(e);
    for (auto &[branch, expr] : local->path()) {
      expressions.push_back(expr);
    }
  }

  auto fingerprint = copy.getFingerprint(expressions);
  bool res = event.set_fingerprint(fingerprint, delta);

#ifdef ENABLE_VERIFIED_FINGERPRINTS
  if (!res) {
    llvm::errs() << MemoryFingerprint::toString(event.fingerprint().diff(fingerprint)) << "\n";
    llvm::errs() << "\n";
    llvm::errs() << MemoryFingerprint::toString(event.thread_delta().diff(delta)) << "\n";
  }

  assert(MemoryFingerprint::validateFingerprint(event.fingerprint()));
#endif

  return res;
}


void PorEventManager::findNewCutoff(ExecutionState &state) {
  if (!state.porNode || !PruneStates) {
    return;
  }

  assert(!state.porNode->has_event() && state.porNode->parent()->has_event());
  const por::event::event &event = *state.porNode->parent()->event();

  if (event.is_cutoff()) {
    if (!state.needsCatchUp()) {
      state.cutoffThread();
      ++stats::cutoffThreads;
    }
    return;
  }

  if (!event.has_fingerprint()) {
    return;
  }

  auto it = fingerprints.find(event.fingerprint());
  if (it == fingerprints.end()) {
    fingerprints.emplace(event.fingerprint(), &event);
    return;
  }

  const por::event::event &other = *it->second;

  std::size_t eventSize = event.local_configuration_size();
  std::size_t otherSize = other.local_configuration_size();

  bool isCutoff;
  if (UseAdequateOrder) {
    isCutoff = por::compare_adequate_total_order(other, event);
  } else {
    isCutoff = otherSize < eventSize;
  }

  if (isCutoff) {
    // state is at cutoff event

    if (DebugStatePruning) {
      llvm::errs() << "[state id: " << state.id << "] corresponding: " << other.to_string(true)
                  << " with fingerprint: " << MemoryFingerprint::toString(other.fingerprint()) << "\n";
      llvm::errs() << "[state id: " << state.id << "]        cutoff: " << event.to_string(true) << "\n"
                  << " with fingerprint: " << MemoryFingerprint::toString(event.fingerprint()) << "\n";
    }

    assert(state.tid() == event.tid());
    if (!state.needsCatchUp()) {
      state.cutoffThread();
      ++stats::cutoffThreads;
    }

    ++stats::cutoffEvents;
    state.porNode->configuration().unfolding()->stats_inc_cutoff_event(event.kind());
    event.mark_as_cutoff();
  }
}
