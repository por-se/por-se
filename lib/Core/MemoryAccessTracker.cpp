#include "MemoryAccessTracker.h"
#include "Memory.h"

using namespace klee;

static const uint64_t NOT_EXECUTED = ~((uint64_t) 0);

MemoryAccessTracker::EpochMemoryAccesses::EpochMemoryAccesses() = default;

MemoryAccessTracker::EpochMemoryAccesses::EpochMemoryAccesses(const EpochMemoryAccesses& ac) {

  owner = ac.owner;
  tid = ac.tid;
  accesses = ac.accesses;
  preThreadAccesses = ac.preThreadAccesses;
  scheduleIndex = ac.scheduleIndex;
}

MemoryAccessTracker::MemoryAccessTracker() = default;
MemoryAccessTracker::MemoryAccessTracker(const MemoryAccessTracker &list) = default;

void MemoryAccessTracker::forkCurrentEpochWhenNeeded() {
  if (accessLists.empty()) {
    return;
  }

  auto last = accessLists.back();
  if (last->owner == this) {
    return;
  }

  std::shared_ptr<EpochMemoryAccesses> ema = std::make_shared<EpochMemoryAccesses>(*last);
  ema->owner = this;

  accessLists[accessLists.size() - 1] = ema;
}

void MemoryAccessTracker::scheduledNewThread(Thread::ThreadId tid) {
  // So basically we have scheduled a new thread: that means
  // making sure the current epoch ends and we start a new one

  // But first of all we want to release our ownership over the last epoch if
  // we are the owner
  if (!accessLists.empty() && accessLists.back()->owner == this) {
    accessLists.back()->owner = nullptr;
  }

  std::shared_ptr<EpochMemoryAccesses> ema = std::make_shared<EpochMemoryAccesses>();
  ema->owner = this;
  ema->tid = tid;
  ema->scheduleIndex = accessLists.size();

  if (tid + 1 > lastExecutions.size()) {
    lastExecutions.resize(tid + 1, NOT_EXECUTED);
  }

  uint64_t exec = lastExecutions[tid];
  if (exec != NOT_EXECUTED) {
    ema->preThreadAccesses = accessLists[exec];
  }

  lastExecutions[tid] = ema->scheduleIndex;
  accessLists.emplace_back(ema);
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

    // Every free or alloc call is stronger as any other access type and does not require
    // offset checks, so this is one of the simpler merges
    if (newIsAlloc || newIsFree) {
      accessIt.type = access.type;
      // alloc and free do not track the offset
      accessIt.offset = nullptr;
      return;
    }

    // One special case where we can merge two entries: the previous one is a read
    // and now a write is done to the same offset (write is stronger)
    // Needs the same offsets to be correct
    if (newIsWrite && (accessIt.type & READ_ACCESS) && access.offset == accessIt.offset){
      accessIt.type = WRITE_ACCESS;
      return;
    }
  }

  MemoryAccess newAccess (access);
  // Make sure that we always use the same epoch number
  accesses.emplace_back(newAccess);
}

void MemoryAccessTracker::registerThreadSync(Thread::ThreadId tid1, Thread::ThreadId tid2, uint64_t epoch) {
  uint64_t* v = getThreadsSyncValue(tid1, tid2);
  *v = epoch;

  // But since these threads are now in sync; we need to rebalance all other threads
  // as well, consider: if one thread has synced  with a third at a later state than
  // the other thread, then we know now for sure that the sync will be transitive:
  // We indirectly sync with the thread through the other one
//  for (auto& threadSyncIt : thread->threadSyncs) {
//    Thread::ThreadId thirdPartyTid = threadSyncIt.first;
//
//    // We just synced them above so safely skip them
//    if (thirdPartyTid == curThreadId) {
//      continue;
//    }
//
//    uint64_t threadSyncedAt = threadSyncIt.second;
//    uint64_t curThreadSyncedAt = currentThread->threadSyncs[thirdPartyTid];
//
//    // Another safe skip as we are at the same state
//    if (threadSyncedAt == curThreadSyncedAt) {
//      continue;
//    }
//
//    auto thThreadPair = threads.find(thirdPartyTid);
//    assert(thThreadPair != threads.end() && "Could not find referenced thread");
//    Thread* thirdPartyThread = &(thThreadPair->second);
//
//    // Now find the one that is more recent than the other and update the values
//    if (threadSyncedAt < curThreadSyncedAt) {
//      thread->threadSyncs[thirdPartyTid] = curThreadSyncedAt;
//      thirdPartyThread->threadSyncs[tid] = curThreadSyncedAt;
//    } else if (threadSyncedAt < curThreadSyncedAt) {
//      currentThread->threadSyncs[thirdPartyTid] = threadSyncedAt;
//      thirdPartyThread->threadSyncs[curThreadId] = threadSyncedAt;
//    }
//  }

  // TODO: try to use this info in order to prune old memory accesses, that we no longer need to keep
}

uint64_t* MemoryAccessTracker::getThreadsSyncValue(Thread::ThreadId tid1, Thread::ThreadId tid2) {
  assert(tid1 != tid2 && "ThreadIds have to be unequal");

  uint64_t min = std::min(tid1, tid2);
  uint64_t max = std::max(tid1, tid2);

  if (max > threadSyncs.size()) {
    threadSyncs.resize(max);
    for (uint64_t i = 0; i < threadSyncs.size(); i++) {
      threadSyncs[i].resize(max - i, 0);
    }
  }

  return &threadSyncs[min][max - min - 1];
}

void MemoryAccessTracker::testIfUnsafeMemAccessByThread(MemAccessSafetyResult &result, Thread::ThreadId tid,
                                                        uint64_t id, MemoryAccess &access) {
  uint64_t exec = lastExecutions[tid];
  if (exec == NOT_EXECUTED) {
    return;
  }

  Thread::ThreadId curTid = accessLists.back()->tid;

  bool isRead = (access.type & READ_ACCESS);
  bool isFree = (access.type & FREE_ACCESS);
  bool isAlloc = (access.type & ALLOC_ACCESS);

  uint64_t sync = *getThreadsSyncValue(tid, curTid);
  auto ema = accessLists[exec];

  while (ema != nullptr && sync < ema->scheduleIndex) {
    assert(ema->tid == tid);
    uint64_t scheduleIndex = ema->scheduleIndex;

    auto objAccesses = ema->accesses.find(id);
    if (objAccesses == ema->accesses.end()) {
      // There was no access to that object in this schedule phase
      ema = ema->preThreadAccesses;
      continue;
    }

    std::vector<MemoryAccess>& accesses = objAccesses->second;
    for (auto& a : accesses) {
      // One access pattern that is especially dangerous is an unprotected free
      // every combination is unsafe (read + free, write + free, ...)
      if (isFree || (a.type & FREE_ACCESS)) {
        if (!a.safeMemoryAccess) {
          result.wasSafe = false;
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
        if (!a.safeMemoryAccess) {
          result.wasSafe = false;
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
        if (!a.safeMemoryAccess) {
          result.wasSafe = false;
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
      if (!a.safeMemoryAccess) {
        result.possibleCandidates.push_back(a);
      }
    }

    ema = ema->preThreadAccesses;
  }
}

MemAccessSafetyResult MemoryAccessTracker::testIfUnsafeMemoryAccess(uint64_t id, MemoryAccess &access) {
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