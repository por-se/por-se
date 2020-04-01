#include "DelayedFreesContainer.h"

#include "por/event/event.h"

using namespace klee;

void DelayedFreesContainer::drainFrees(const por::event::event &newEvt, FreeCallback callback) {
  // So we want to find all events (except from events that we triggered) and
  // check if there are any frees we have to perform

  if (newEvt.kind() == por::event::event_kind::thread_init) {
    // So we only init the current thread, therefore, there cannot
    // be any memory objects that we had created before
    return;
  }

  for (const auto *pred : newEvt.synchronized_events()) {
    auto& delayedFreesPerThread = pred->metadata().pending_frees;
    // Check if there is something for this thread
    auto it = delayedFreesPerThread.find(newEvt.tid());
    if (it != delayedFreesPerThread.end()) {
      for (const auto* mo : it->second) {
        callback(mo);
      }
    }
  }
}
