#pragma once

#include "klee/ThreadId.h"

#include <map>
#include <functional>

namespace por::event {
  struct event;
}

namespace klee {
  class MemoryObject;

  class DelayedFreesContainer {
    public:
      using FreeCallback = std::function<void(const MemoryObject*)>;

    private:
      // Map that keeps track of which events we already checked
      std::map<ThreadId, std::map<ThreadId, const por::event::event*>> alreadyDone;

    public:
      DelayedFreesContainer() = default;

      void drainFrees(const por::event::event &newEvt, FreeCallback callback);
  };
}
