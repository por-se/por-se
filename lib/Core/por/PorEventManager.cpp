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

void PorEventManager::logEventThreadAndKind(const ExecutionState &state, por::event::event_kind kind) {
  llvm::errs() << "[state id: " << state.id << "] ";
  llvm::errs() << "registering " << kind << " with current thread " << state.tid();
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

bool PorEventManager::registerNonLocal(ExecutionState &state, por::extension &&ex, bool snapshotsAllowed) {
  if (DebugEventRegistration) {
    llvm::errs() << "[state id: " << state.id << "] ";
    llvm::errs() << "POR event: " << ex.event->to_string(true) << "\n";
  }

  assert(!state.hasUnregisteredDecisions());

  state.needsThreadScheduling = true;

  // attach metadata
  attachFingerprintToEvent(state, *ex.event.get());
  const por::event::metadata newMetadata = ex.event->metadata();

  // create standby state
  std::shared_ptr<const ExecutionState> standby;
  if (snapshotsAllowed) {
    standby = createStandbyState(state, ex.event->kind());
  }

  // deduplicate events, update porNode
  bool catchingUp = state.needsCatchUp();
  if (catchingUp) {
    state.porNode = state.porNode->catch_up(std::move(ex), std::move(standby), state.peekCatchUp());
    if (state.porNode == nullptr) {
      return false;
    }
    state.catchUp.pop_front();
  } else {
    state.porNode = state.porNode->make_left_child(std::move(ex), std::move(standby));
  }

  // compare metadata after deduplication
  auto event = state.porNode->parent()->event();
  bool res = event->metadata() == newMetadata;

  if (!res) {
#ifdef ENABLE_VERIFIED_FINGERPRINTS
    llvm::errs() << MemoryFingerprint::toString(event->metadata().fingerprint.diff(newMetadata.fingerprint)) << "\n";
    llvm::errs() << "\n";
    llvm::errs() << MemoryFingerprint::toString(event->metadata().thread_delta.diff(newMetadata.thread_delta)) << "\n";
#endif
    return false;
  }

  // identify new cutoff events
  if (!catchingUp) {
    findNewCutoff(state);
  }

  return true;
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

    auto path = std::move(state.unregisteredDecisions());
    state.unregisteredDecisions() = {};

    por::extension ex = state.porNode->configuration().local(state.tid(), std::move(path));

    // attach metadata
    attachFingerprintToEvent(state, *ex.event.get());
    const por::event::metadata newMetadata = ex.event->metadata();

    // create standby state
    std::shared_ptr<const ExecutionState> standby;
    if (snapshotsAllowed) {
      standby = createStandbyState(state, por::event::event_kind::local);
    }

    // deduplicate events, update porNode
    state.porNode = state.porNode->catch_up(std::move(ex), std::move(standby), state.peekCatchUp());
    if (state.porNode == nullptr) {
      return false;
    }
    state.catchUp.pop_front();

    // compare metadata after deduplication
    auto event = state.porNode->parent()->event();
    success = event->metadata() == newMetadata;

    if (!success) {
  #ifdef ENABLE_VERIFIED_FINGERPRINTS
      llvm::errs() << MemoryFingerprint::toString(event->metadata().fingerprint.diff(newMetadata.fingerprint)) << "\n";
      llvm::errs() << "\n";
      llvm::errs() << MemoryFingerprint::toString(event->metadata().thread_delta.diff(newMetadata.thread_delta)) << "\n";
  #endif
      return false;
    }

    return true;
  }

  por::node *n = state.porNode;

  auto path = std::move(state.unregisteredDecisions());
  state.unregisteredDecisions() = {};

  por::extension ex = state.porNode->configuration().local(state.tid(), std::move(path));

  // attach metadata
  attachFingerprintToEvent(state, *ex.event.get());
  const por::event::metadata newMetadata = ex.event->metadata();

  // create standby state
  std::shared_ptr<const ExecutionState> standby;
  if (snapshotsAllowed) {
    standby = createStandbyState(state, por::event::event_kind::local);
  }

  // deduplicate events, update porNode
  state.porNode = state.porNode->make_left_child(std::move(ex), std::move(standby));

  // compare metadata after deduplication
  auto event = state.porNode->parent()->event();
  success = event->metadata() == newMetadata;

  if (!success) {
#ifdef ENABLE_VERIFIED_FINGERPRINTS
    llvm::errs() << MemoryFingerprint::toString(event->metadata().fingerprint.diff(newMetadata.fingerprint)) << "\n";
    llvm::errs() << "\n";
    llvm::errs() << MemoryFingerprint::toString(event->metadata().thread_delta.diff(newMetadata.thread_delta)) << "\n";
#endif
    return false;
  }

  // identify new cutoff events
  findNewCutoff(state);

  assert(state.porNode->parent() == n);

  for (auto& s : addedStates) {
    if (!success) {
      return false;
    }
    if (s->hasUnregisteredDecisions()) {
      s->porNode = n; // needed for distance calculation in shouldRegisterStandbyState

      auto path = std::move(s->unregisteredDecisions());
      s->unregisteredDecisions() = {};

      por::extension ex = n->configuration().local(s->tid(), std::move(path));

      // attach metadata
      attachFingerprintToEvent(*s, *ex.event.get());
      const por::event::metadata newMetadata = ex.event->metadata();

      // create standby state
      std::shared_ptr<const ExecutionState> standby;
      if (snapshotsAllowed) {
        standby = createStandbyState(*s, por::event::event_kind::local);
      }

      // deduplicate events, update porNode
      s->porNode = n->make_right_local_child(std::move(ex), std::move(standby));

      // compare metadata after deduplication
      auto event = s->porNode->parent()->event();
      bool res = event->metadata() == newMetadata;

      if (!res) {
#ifdef ENABLE_VERIFIED_FINGERPRINTS
        llvm::errs() << MemoryFingerprint::toString(event->metadata().fingerprint.diff(newMetadata.fingerprint)) << "\n";
        llvm::errs() << "\n";
        llvm::errs() << MemoryFingerprint::toString(event->metadata().thread_delta.diff(newMetadata.thread_delta)) << "\n";
#endif
        success = false;
      }

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

  por::extension ex = state.porNode->configuration().create_thread(state.tid(), tid);
  return registerNonLocal(state, std::move(ex));
}

bool PorEventManager::registerThreadInit(ExecutionState &state, const ThreadId &tid) {
  if (DebugEventRegistration) {
    logEventThreadAndKind(state, por::event::event_kind::thread_init);

    llvm::errs() << " and initialized thread " << tid << "\n";
  }

  bool success = true;
  if (tid == ExecutionState::mainThreadId) {
    // event already present in configuration
    assert(!state.hasUnregisteredDecisions());
    state.needsThreadScheduling = true;

    // update porNode
    state.porNode = state.porNode->make_left_child(createStandbyState(state, por::event::event_kind::thread_init));

    // retrieve already present event
    auto event = const_cast<por::event::event*>(state.porNode->last_included_event());
    assert(event->kind() == por::event::event_kind::thread_init && event->tid() == ExecutionState::mainThreadId);

    if (DebugEventRegistration) {
      llvm::errs() << "[state id: " << state.id << "] ";
      llvm::errs() << "POR event: " << event->to_string(true) << "\n";
    }

    attachFingerprintToEvent(state, *event);
  } else {
    assert(state.tid() != tid);
    por::extension ex = state.porNode->configuration().init_thread(tid, state.tid());
    success = registerNonLocal(state, std::move(ex));
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

  por::extension ex = state.porNode->configuration().exit_thread(tid, atomic);
  bool success = registerNonLocal(state, std::move(ex));

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

  por::extension ex = state.porNode->configuration().join_thread(state.tid(), joinedThread);
  return registerNonLocal(state, std::move(ex));
}


bool PorEventManager::registerLockCreate(ExecutionState &state, std::uint64_t mId) {
  if (DebugEventRegistration) {
    logEventThreadAndKind(state, por::event::event_kind::lock_create);

    llvm::errs() << " on mutex " << mId << "\n";
  }

  por::extension ex = state.porNode->configuration().create_lock(state.tid(), mId);
  return registerNonLocal(state, std::move(ex));
}

bool PorEventManager::registerLockDestroy(ExecutionState &state, std::uint64_t mId) {
  if (DebugEventRegistration) {
    logEventThreadAndKind(state, por::event::event_kind::lock_destroy);

    llvm::errs() << " on mutex " << mId << "\n";
  }

  por::extension ex = state.porNode->configuration().destroy_lock(state.tid(), mId);
  return registerNonLocal(state, std::move(ex));
}

bool PorEventManager::registerLockAcquire(ExecutionState &state, std::uint64_t mId, bool snapshotsAllowed) {
  if (DebugEventRegistration) {
    logEventThreadAndKind(state, por::event::event_kind::lock_acquire);

    llvm::errs() << " on mutex " << mId << "\n";
  }

  por::extension ex = state.porNode->configuration().acquire_lock(state.tid(), mId);
  return registerNonLocal(state, std::move(ex), snapshotsAllowed);
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

  por::extension ex = state.porNode->configuration().release_lock(state.tid(), mId, atomic);
  bool success = registerNonLocal(state, std::move(ex), snapshot);

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

  por::extension ex = state.porNode->configuration().create_cond(state.tid(), cId);
  return registerNonLocal(state, std::move(ex));
}

bool PorEventManager::registerCondVarDestroy(ExecutionState &state, std::uint64_t cId) {
  if (DebugEventRegistration) {
    logEventThreadAndKind(state, por::event::event_kind::condition_variable_destroy);

    llvm::errs() << " on cond. var " << cId << "\n";
  }

  por::extension ex = state.porNode->configuration().destroy_cond(state.tid(), cId);
  return registerNonLocal(state, std::move(ex));
}

bool PorEventManager::registerCondVarSignal(ExecutionState &state, std::uint64_t cId, const ThreadId& notifiedThread) {
  if (DebugEventRegistration) {
    logEventThreadAndKind(state, por::event::event_kind::signal);

    llvm::errs() << " on cond. var " << cId << " and signalled thread " << notifiedThread << "\n";
  }

  por::extension ex = state.porNode->configuration().signal_thread(state.tid(), cId, notifiedThread);
  return registerNonLocal(state, std::move(ex));
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

  por::extension ex = state.porNode->configuration().broadcast_threads(state.tid(), cId, threads);
  return registerNonLocal(state, std::move(ex));
}

bool PorEventManager::registerCondVarWait1(ExecutionState &state, std::uint64_t cId, std::uint64_t mId) {
  if (DebugEventRegistration) {
    logEventThreadAndKind(state, por::event::event_kind::wait1);

    llvm::errs() << " on cond. var " << cId << " and mutex " << mId << "\n";
  }

  por::extension ex = state.porNode->configuration().wait1(state.tid(), cId, mId);
  return registerNonLocal(state, std::move(ex));
}

bool PorEventManager::registerCondVarWait2(ExecutionState &state, std::uint64_t cId, std::uint64_t mId) {
  if (DebugEventRegistration) {
    logEventThreadAndKind(state, por::event::event_kind::wait2);

    llvm::errs() << " on cond. var " << cId << " and mutex " << mId << "\n";
  }

  por::extension ex = state.porNode->configuration().wait2(state.tid(), cId, mId);
  return registerNonLocal(state, std::move(ex));
}

void PorEventManager::attachFingerprintToEvent(ExecutionState &state, por::event::event &event) {
  if (!PruneStates) {
    return;
  }

  auto thread = state.getThreadById(event.tid());
  assert(thread && "no thread with given id found");

  MemoryFingerprint copy;
  auto delta = thread->get().getFingerprintDelta();
  copy.addDelta(delta);

  for (auto &[tid, c] : event.cone()) {
    if (tid != event.tid()) {
      copy.addDelta(c->metadata().thread_delta);
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
  event.set_metadata({fingerprint, delta});

#ifdef ENABLE_VERIFIED_FINGERPRINTS
  assert(MemoryFingerprint::validateFingerprint(event.metadata().fingerprint));
#endif
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

  auto it = fingerprints.find(event.metadata().fingerprint);
  if (it == fingerprints.end()) {
    fingerprints.emplace(event.metadata().fingerprint, &event);
    return;
  }

  const por::event::event &other = *it->second;

  bool isCutoff;
  if (UseAdequateOrder) {
    isCutoff = por::compare_adequate_total_order(other, event);
  } else {
    isCutoff = other.local_configuration_size() < event.local_configuration_size();
  }

  if (isCutoff) {
    // state is at cutoff event

    if (DebugStatePruning) {
      llvm::errs() << "[state id: " << state.id << "] corresponding: " << other.to_string(true)
                  << " with fingerprint: " << MemoryFingerprint::toString(other.metadata().fingerprint) << "\n";
      llvm::errs() << "[state id: " << state.id << "]        cutoff: " << event.to_string(true) << "\n"
                  << " with fingerprint: " << MemoryFingerprint::toString(event.metadata().fingerprint) << "\n";
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
