#pragma once

#include "klee/ThreadId.h"

#include <map>
#include <vector>
#include <optional>
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
      using FreeList = std::vector<const MemoryObject*>;

      // List of frees that could not yet be registered to events, since the event
      // is not yet known
      /// @brief map of thread to the list of delayed frees performed by the thread
      std::map<ThreadId, FreeList> pending;

      // Map that keeps track of which events we already checked
      std::map<ThreadId, std::map<ThreadId, const por::event::event*>> alreadyDone;

    public:
      DelayedFreesContainer() = default;

      void registerFree(const ThreadId& tid, const MemoryObject* freedObject);

      [[nodiscard]]
      std::map<ThreadId, std::vector<const MemoryObject*>>
      flushUnregistredFrees(const por::event::event &event);

      void drainFrees(const por::event::event &newEvt, FreeCallback callback);
  };
}
