#pragma once

#include <functional>

namespace por::event {
  struct event;
}

namespace klee {
  class MemoryObject;

  class DelayedFreesContainer {
    public:
      using FreeCallback = std::function<void(const MemoryObject*)>;

      DelayedFreesContainer() = default;

      void drainFrees(const por::event::event &newEvt, FreeCallback callback);
  };
}
