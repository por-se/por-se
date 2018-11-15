#ifndef KLEE_MEMORYACCESSTRACKER_H
#define KLEE_MEMORYACCESSTRACKER_H

#include "klee/Expr.h"
#include "klee/Thread.h"

#include <vector>
#include <map>

namespace klee {
  /// @brief the actual struct to hold the memory accesses
  struct MemoryAccess {
    uint8_t type = 0;
    ref<Expr> offset;

    bool atomicMemoryAccess = false;
    bool safeMemoryAccess = false;

    MemoryAccess() = default;
    MemoryAccess(const MemoryAccess &a) = default;
  };

  struct MemAccessSafetyResult {
    bool wasSafe = false;

    std::vector<MemoryAccess> possibleCandidates;
    std::map<Thread::ThreadId, uint64_t> dataDependencies;

    MemAccessSafetyResult() = default;
    MemAccessSafetyResult(const MemAccessSafetyResult& sr) = default;
  };

  class MemoryAccessTracker {
    public:
      static const uint8_t READ_ACCESS = 1;
      static const uint8_t WRITE_ACCESS = 2;
      static const uint8_t FREE_ACCESS = 4;
      static const uint8_t ALLOC_ACCESS = 8;

    private:
      struct EpochMemoryAccesses {
        void* owner;
        Thread::ThreadId tid;
        uint64_t scheduleIndex;

        uint64_t preThreadAccessIndex;
        std::map<uint64_t, std::vector<MemoryAccess>> accesses;

        EpochMemoryAccesses() = default;
        EpochMemoryAccesses(const EpochMemoryAccesses& ac);
      };

      std::vector<std::shared_ptr<EpochMemoryAccesses>> accessLists;

      std::vector<std::vector<uint64_t>> threadSyncs;

      std::set<Thread::ThreadId> knownThreads;
      std::vector<uint64_t> lastExecutions;

      void forkCurrentEpochWhenNeeded();

      uint64_t* getThreadSyncValueTo(Thread::ThreadId tid, Thread::ThreadId reference);

      void testIfUnsafeMemAccessByThread(MemAccessSafetyResult &result, Thread::ThreadId tid,
                                         uint64_t id, const MemoryAccess &access);

    public:
      MemoryAccessTracker() = default;
      MemoryAccessTracker(const MemoryAccessTracker& list) = default;

      void scheduledNewThread(Thread::ThreadId tid);

      void trackMemoryAccess(uint64_t id, MemoryAccess access);

      void registerThreadDependency(Thread::ThreadId tid1, Thread::ThreadId tid2, uint64_t epoch);

      MemAccessSafetyResult testIfUnsafeMemoryAccess(uint64_t id, const MemoryAccess &access);
  };
}

#endif //KLEE_MEMORYACCESSTRACKER_H
