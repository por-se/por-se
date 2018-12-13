#ifndef KLEE_MEMORYACCESSTRACKER_H
#define KLEE_MEMORYACCESSTRACKER_H

#include "klee/Expr.h"
#include "klee/Thread.h"
#include "klee/Internal/Module/KInstruction.h"

#include <vector>
#include <map>

namespace klee {
  /// @brief the actual struct to hold the memory accesses
  struct MemoryAccess {
    uint8_t type = 0;
    ref<Expr> offset;

    bool atomicMemoryAccess = false;
    bool safeMemoryAccess = false;
    KInstruction* instruction = nullptr;

    MemoryAccess() = default;
    MemoryAccess(const MemoryAccess &a) = default;
  };

  struct MemAccessSafetyResult {
    bool wasSafe = false;
    MemoryAccess racingAccess;

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
        /// @brief a reference to the entity that is allowed to write to this possibly shared object
        // Note: in the case that the epoch is finished (therefore finished -> write protected), this becomes nullptr
        void* cowOwner;
        Thread::ThreadId tid;
        uint64_t scheduleIndex;

        /// @brief index in the scheduling history that this epoch was executed the last time
        // Note: this is not a pointer to the previous one since that might keep around this fragment
        //       later if it is no longer needed as it will be part of a chain
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

      /// @brief returns the latest epoch of the `reference` thread that `tid` thread has a dependency to
      uint64_t* getThreadSyncValueTo(Thread::ThreadId tid, Thread::ThreadId reference);

      void testIfUnsafeMemAccessByThread(MemAccessSafetyResult &result, Thread::ThreadId tid,
                                         uint64_t id, const MemoryAccess &access);

    public:
      MemoryAccessTracker() = default;
      MemoryAccessTracker(const MemoryAccessTracker& list) = default;

      void scheduledNewThread(Thread::ThreadId tid);

      void trackMemoryAccess(uint64_t id, MemoryAccess access);

      void registerThreadDependency(Thread::ThreadId targetTid, Thread::ThreadId predTid, uint64_t epoch);

      MemAccessSafetyResult testIfUnsafeMemoryAccess(uint64_t id, const MemoryAccess &access);
  };
}

#endif //KLEE_MEMORYACCESSTRACKER_H
