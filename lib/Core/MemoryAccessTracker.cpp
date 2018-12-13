#include "MemoryAccessTracker.h"
#include "Memory.h"

using namespace klee;

static const uint64_t NOT_EXECUTED = ~((uint64_t) 0);

MemoryAccessTracker::EpochMemoryAccesses::EpochMemoryAccesses(const EpochMemoryAccesses& ac) = default;

void MemoryAccessTracker::forkCurrentEpochWhenNeeded() {
  if (accessLists.empty()) {
    return;
  }

  auto last = accessLists.back();
  if (last->cowOwner == this) {
    return;
  }

  std::shared_ptr<EpochMemoryAccesses> ema = std::make_shared<EpochMemoryAccesses>(*last);
  ema->cowOwner = this;

  accessLists[accessLists.size() - 1] = ema;
}

void MemoryAccessTracker::scheduledNewThread(Thread::ThreadId tid) {
  // So basically we have scheduled a new thread: that means
  // making sure the current epoch ends and we start a new one

  // But first of all we want to release our ownership over the last epoch if
  // we are the owner
  if (!accessLists.empty() && accessLists.back()->cowOwner == this) {
    accessLists.back()->cowOwner = nullptr;
  }

  std::shared_ptr<EpochMemoryAccesses> ema = std::make_shared<EpochMemoryAccesses>();
  ema->cowOwner = this;
  ema->tid = tid;
  ema->scheduleIndex = accessLists.size();

  if (tid + 1 > lastExecutions.size()) {
    lastExecutions.resize(tid + 1, NOT_EXECUTED);
  }

  uint64_t exec = lastExecutions[tid];
  ema->preThreadAccessIndex = exec;

  lastExecutions[tid] = ema->scheduleIndex;
  accessLists.emplace_back(ema);

  knownThreads.insert(tid);
}

void MemoryAccessTracker::trackMemoryAccess(uint64_t id, MemoryAccess access) {
  assert(!accessLists.empty() && "A scheduled thread should be");

  forkCurrentEpochWhenNeeded();

  std::shared_ptr<EpochMemoryAccesses>& ema = accessLists.back();
  std::vector<MemoryAccess>& accesses = ema->accesses[id];

  bool newIsWrite = access.type & WRITE_ACCESS;
  bool newIsFree = access.type & FREE_ACCESS;
  bool newIsAlloc = access.type & ALLOC_ACCESS;

  // So there is already an entry. So go ahead and deduplicate as much as possible
  for (auto& accessIt : accesses) {
    // Another important difference we should always consider is when two different accesses
    // conflict with their scheduling configuration
    if (accessIt.safeMemoryAccess != access.safeMemoryAccess) {
      continue;
    }
    if (accessIt.atomicMemoryAccess != access.atomicMemoryAccess) {
      continue;
    }

    // Every free or alloc call is stronger as any other access type and does not require
    // offset checks, so this is one of the simpler merges
    if (newIsAlloc || newIsFree) {
      accessIt.type = access.type;
      // alloc and free do not track the offset
      accessIt.offset = nullptr;
      accessIt.instruction = access.instruction;
      return;
    }

    // One special case where we can merge two entries: the previous one is a read
    // and now a write is done to the same offset (write is stronger)
    // Needs the same offsets to be correct
    if (newIsWrite && (accessIt.type & READ_ACCESS) && access.offset == accessIt.offset){
      accessIt.type = WRITE_ACCESS;
      accessIt.instruction = access.instruction;
      return;
    }
  }

  // Make sure that we always use the same epoch number
  accesses.emplace_back(access);
}

void MemoryAccessTracker::registerThreadDependency(Thread::ThreadId targetTid, Thread::ThreadId predTid, uint64_t epoch) {
  // Tries to register the following ordering: predTid > targetTid
  uint64_t* v = getThreadSyncValueTo(targetTid, predTid);

  if (*v >= epoch) {
    return;
  }

  *v = epoch;

  // Two threads can also synchronize through a third one
  for (uint64_t thirdTid = 0; thirdTid < knownThreads.size(); thirdTid++) {
    if (thirdTid == targetTid || thirdTid == predTid) {
      // We want to find a third thread, therefore we can ignore these
      continue;
    }

    // Get the reference values
    // value of: thirdTid < predTid
    uint64_t transitiveRef = *getThreadSyncValueTo(predTid, thirdTid);
    // value of: thirdTid < targetTid
    uint64_t* currentRef = getThreadSyncValueTo(targetTid, thirdTid);

    // Check whether the transitive is actually better and whether the transitive can be applied
    if (transitiveRef > *currentRef && transitiveRef < epoch) {
      *currentRef = transitiveRef;
    }
  }

  // As we have added a new relation, it is possible that certain memory accesses no longer need to
  // be tracked.
  // We calculate the earliest epoch in which there exists a dependency between any two threads.
  // Everything before that is basically no longer needed and can be unreferenced.
  uint64_t keepAllAfter = accessLists.size();

  for (uint64_t i = 0; i < knownThreads.size(); i++) {
    for (uint64_t j = 0; j < knownThreads.size(); j++) {
      if (i == j) {
        continue;
      }

      uint64_t rel = *getThreadSyncValueTo(i, j);
      if (rel < keepAllAfter) {
        keepAllAfter = rel;
      }
    }
  }

  if (keepAllAfter < accessLists.size()) {
    for (uint64_t i = 0; i <= keepAllAfter; i++) {
      accessLists[i] = nullptr;
    }
  }
}

uint64_t* MemoryAccessTracker::getThreadSyncValueTo(Thread::ThreadId tid, Thread::ThreadId reference) {
  assert(tid != reference && "ThreadIds have to be unequal");
  uint64_t max = std::max(tid, reference);

  // Skip the combination of reference == tid by offsetting the indexes by 1 correspondingly
  if (max + 1 > threadSyncs.size()) {
    threadSyncs.resize(max + 1);

    for (auto &threadSync : threadSyncs) {
      threadSync.resize(max + 1, 0);
    }
  }

  return &threadSyncs[tid][reference];
}

void MemoryAccessTracker::testIfUnsafeMemAccessByThread(MemAccessSafetyResult &result, Thread::ThreadId tid,
                                                        uint64_t id, const MemoryAccess &access) {
  uint64_t exec = lastExecutions[tid];
  if (exec == NOT_EXECUTED) {
    return;
  }

  Thread::ThreadId curTid = accessLists.back()->tid;

  bool isRead = (access.type & READ_ACCESS);
  bool isFree = (access.type & FREE_ACCESS);
  bool isAlloc = (access.type & ALLOC_ACCESS);

  uint64_t sync = *getThreadSyncValueTo(curTid, tid);
  auto ema = accessLists[exec];

  while (ema != nullptr && sync < ema->scheduleIndex) {
    assert(ema->tid == tid);
    uint64_t scheduleIndex = ema->scheduleIndex;

    auto objAccesses = ema->accesses.find(id);
    if (objAccesses == ema->accesses.end()) {
      // There was no access to that object in this schedule phase, move to the next
      if (ema->preThreadAccessIndex == NOT_EXECUTED) {
        break;
      }
      ema = accessLists[ema->preThreadAccessIndex];
      continue;
    }

    std::vector<MemoryAccess>& accesses = objAccesses->second;
    for (auto& a : accesses) {
      // One access pattern that is especially dangerous is an unprotected free
      // every combination is unsafe (read + free, write + free, ...)
      if (isFree || (a.type & FREE_ACCESS)) {
        if (!a.safeMemoryAccess || (a.atomicMemoryAccess ^ access.atomicMemoryAccess)) {
          result.wasSafe = false;
          result.racingAccess = a;
          return;
        }

        uint64_t cur = result.dataDependencies[tid];
        if (cur < scheduleIndex) {
          result.dataDependencies[tid] = scheduleIndex;
        }
        continue;
      }

      // Another unsafe memory access pattern: the operation is not explicitly
      // ordered with the other thread and thus can happen in the reverse order
      if (isAlloc || (a.type & ALLOC_ACCESS)) {
        if (!a.safeMemoryAccess || (a.atomicMemoryAccess ^ access.atomicMemoryAccess)) {
          result.wasSafe = false;
          result.racingAccess = a;
          return;
        }

        uint64_t cur = result.dataDependencies[tid];
        if (cur < scheduleIndex) {
          result.dataDependencies[tid] = scheduleIndex;
        }
        continue;
      }

      // There is one safe memory access pattern:
      // read + read -> so we can skip these
      bool recIsRead = (a.type & READ_ACCESS);
      if (isRead && recIsRead) {
        continue;
      }

      if (a.offset == access.offset) {
        if (!a.safeMemoryAccess || (a.atomicMemoryAccess ^ access.atomicMemoryAccess)) {
          result.wasSafe = false;
          result.racingAccess = a;
          return;
        }

        uint64_t cur = result.dataDependencies[tid];
        if (cur < scheduleIndex) {
          result.dataDependencies[tid] = scheduleIndex;
        }
        continue;
      }

      // So the offset Expr are not the same but we maybe can get
      // a result that is the same
      // But: filter out Const + Const pairs
      if (isa<ConstantExpr>(access.offset) && isa<ConstantExpr>(a.offset)) {
        continue;
      }

      // So add it to the ones we want to test
      if (!a.safeMemoryAccess || (a.atomicMemoryAccess ^ access.atomicMemoryAccess)) {
        result.possibleCandidates.push_back(a);
      }
    }

    if (ema->preThreadAccessIndex == NOT_EXECUTED) {
      break;
    }
    ema = accessLists[ema->preThreadAccessIndex];
  }
}

MemAccessSafetyResult MemoryAccessTracker::testIfUnsafeMemoryAccess(uint64_t id, const MemoryAccess &access) {
  assert(!accessLists.empty() && "There should be at least one scheduling phase");

  MemAccessSafetyResult result;
  result.wasSafe = true;

  auto epochIterator = accessLists.rbegin();
  Thread::ThreadId curTid = (*epochIterator)->tid;

  for (auto& tid : knownThreads) {
    if (tid == curTid) {
      continue;
    }

    testIfUnsafeMemAccessByThread(result, tid, id, access);

    if (!result.wasSafe) {
      result.possibleCandidates.clear();
      return result;
    }
  }

  return result;
}
