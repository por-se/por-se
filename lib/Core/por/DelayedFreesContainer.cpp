#include "DelayedFreesContainer.h"

#include "por/event/event.h"
#include "../Memory.h"

#include "cassert"

using namespace klee;

void DelayedFreesContainer::registerFree(const ThreadId& tid, const MemoryObject* freedObject) {
  const auto& allocatedBy = freedObject->getAllocationStackFrame().first;
  assert(tid != allocatedBy && "Should have been freed directly");
  
  auto& freeList = pending[tid];
  freeList.emplace_back(freedObject);
}

void DelayedFreesContainer::registerPorEvent(const por::event::event &event) {
  // Clear our pending data
  std::map<ThreadId, FreeList> sortedByThread;

  auto it = pending.find(event.tid());
  if (it == pending.end()) {
    return;
  }

  for (const auto* mo : it->second) {
    const auto& allocatingThread = mo->getAllocationStackFrame().first;
    sortedByThread[allocatingThread].emplace_back(mo);
  }

  pending.clear();
  data.emplace(&event, sortedByThread);
}

void DelayedFreesContainer::drainFrees(const por::event::event &newEvt, FreeCallback callback) {
  // So we want to find all events (except from events that we triggered) and
  // check if their are any frees we have to perform

  if (newEvt.kind() == por::event::event_kind::thread_init) {
    // So we only init the current thread, therefore, their cannot
    // be any memory objects that we had created before
    return;
  }

  auto& checkedThreads = alreadyDone[newEvt.tid()];

  for (const auto& it : newEvt.cone()) {
    if (it.first == newEvt.tid()) {
      continue;
    }

    const auto* checked = checkedThreads[it.first];

    const auto* cur = it.second;
    for (; cur != nullptr && (checked == nullptr || checked->is_less_than(*cur)); cur = cur->thread_predecessor()) {
      auto evtIt = data.find(cur);
      if (evtIt == data.end()) {
        continue;
      }

      auto& delayedFreesPerThread = evtIt->second;

      // Check if there is something for this thread
      auto it = delayedFreesPerThread.find(newEvt.tid());
      if (it == delayedFreesPerThread.end()) {
        continue;
      }

      for (const auto* mo : it->second) {
        callback(mo);
      }
    }

    // Make sure that we will not check this event a second time
    checkedThreads[it.first] = it.second;
  }
}
