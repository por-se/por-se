#include "DelayedFreesContainer.h"

#include "por/event/event.h"
#include "../Memory.h"

#include "cassert"

using namespace klee;

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
      auto& delayedFreesPerThread = cur->metadata().pending_frees;

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