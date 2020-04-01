//===-- ExecutionState.cpp ------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Memory.h"

#include "klee/ExecutionState.h"

#include "klee/Expr/Expr.h"
#include "klee/Internal/Module/Cell.h"
#include "klee/Internal/Module/InstructionInfoTable.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/KModule.h"
#include "klee/Internal/Support/DebugPrinter.h"
#include "klee/OptionCategories.h"
#include "klee/StatePruningCmdLine.h"
#include "klee/Thread.h"

#include "CoreStats.h"
#include "Memory.h"
#include "MemoryState.h"
#include "MemoryManager.h"
#include "PTree.h"

#include "llvm/IR/Function.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

#include "por/event/event.h"
#include "por/csd.h"
#include "por/node.h"

#include <algorithm>
#include <cassert>
#include <cstdarg>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>

using namespace klee;

namespace {
  llvm::cl::opt<bool>
  DebugThreadTransitions("debug-thread-transitions",
                         llvm::cl::init(false),
                         llvm::cl::cat(DebugCat));
} // namespace

size_t ExecutionState::next_id = 0;
const ThreadId ExecutionState::mainThreadId{ThreadId(), 1};

/***/

ExecutionState::ExecutionState(KFunction *kf) :
    id(next_id++),
    currentSchedulingIndex(0),
    depth(0),
    instsSinceCovNew(0),
    coveredNew(false),
    forkDisabled(false),
    ptreeNode(0),
    memoryState(this),
    steppedInstructions(0),
    threadsCreated(0)
{
    auto result = threads.emplace(std::piecewise_construct,
                                  std::forward_as_tuple(mainThreadId),
                                  std::forward_as_tuple(mainThreadId, kf));
    assert(result.second);
    runThread(result.first->second);
    assert(current);
}


ExecutionState::ExecutionState(const std::vector<ref<Expr> > &assumptions)
    : id(next_id++), constraints(assumptions), ptreeNode(nullptr),
      memoryState(this) {}

ExecutionState::~ExecutionState() {
  // We have to clean up all stack frames of all threads
  for (auto& [_, thread] : threads) {
    while (!thread.stack.empty()) {
      popFrameOfThread(thread);
    }
  }
}

ExecutionState::ExecutionState(const ExecutionState& state):
    id(next_id++),
    currentSchedulingIndex(state.currentSchedulingIndex),
    raceDetection(state.raceDetection),
    threads(state.threads),
    needsThreadScheduling(state.needsThreadScheduling),
    calledExit(state.calledExit),
    schedulingHistory(state.schedulingHistory),

    addressSpace(state.addressSpace),
    constraints(state.constraints),

    queryCost(state.queryCost),
    depth(state.depth),

    pathOS(state.pathOS),
    symPathOS(state.symPathOS),

    instsSinceCovNew(state.instsSinceCovNew),
    coveredNew(state.coveredNew),
    forkDisabled(state.forkDisabled),
    coveredLines(state.coveredLines),
    ptreeNode(state.ptreeNode),
    symbolics(state.symbolics),
    arrayNames(state.arrayNames),
    memoryState(state.memoryState, this),
    lastPorNode(state.porNode),
    catchUp(state.catchUp),
    steppedInstructions(state.steppedInstructions),
    threadsCreated(state.threadsCreated)
{
  current = &threads.at(state.tid());

}

ExecutionState::ExecutionState(const por::leaf &leaf) : ExecutionState(*leaf.start->standby_state()) {
  catchUp = leaf.catch_up;
  porNode = leaf.start;
  ++depth;
  coveredNew = false;
  coveredLines.clear();
}

ExecutionState *ExecutionState::branch() {
  depth++;

  ExecutionState *falseState = new ExecutionState(*this);
  falseState->coveredNew = false;
  falseState->coveredLines.clear();

  return falseState;
}

void ExecutionState::popFrameOfThread(Thread &thread) {
  StackFrame &sf = thread.stack.back();

  for (auto it = sf.allocas.rbegin(), end = sf.allocas.rend(); it != end; it++) {
    const MemoryObject* mo = *it;

    mo->parent->deallocate(mo, thread);
    addressSpace.unbindObject(mo);
  }

  if (PruneStates && porNode) {
    memoryState.registerPopFrame(sf);
  }

  // Let the thread class handle the rest
  thread.popStackFrame();
}

Thread &ExecutionState::createThread(KFunction *kf, ref<Expr> runtimeStructPtr) {
  ThreadId newTid = ThreadId(tid(), ++thread().spawnedThreads);

  auto result = threads.emplace(std::piecewise_construct,
                                std::forward_as_tuple(newTid),
                                std::forward_as_tuple(newTid, kf));
  assert(result.second);

  Thread &newThread = result.first->second;
  newThread.runtimeStructPtr = runtimeStructPtr;

  ++threadsCreated;

  return newThread;
}

void ExecutionState::exitThread(bool callToExit) {
  if (DebugThreadTransitions) {
    llvm::errs() << "[state id: " << id << "] Exiting thread " << current->getThreadId().to_string() << "\n";
  }

  assert(current->state == ThreadState::Runnable);
  current->state = ThreadState::Exited;
  needsThreadScheduling = true;

  if (callToExit) {
    calledExit = true;
  }

   while (!current->stack.empty()) {
    popFrameOfThread(*current);
  }
}

void ExecutionState::cutoffThread(Thread &thread) {
  if (DebugThreadTransitions) {
    llvm::errs() << "[state id: " << id << "] Cutting off thread " << current->getThreadId().to_string() << "\n";
  }

  thread.state = ThreadState::Cutoff;
  needsThreadScheduling = true;
}

void ExecutionState::blockThread(Thread &thread, Thread::waiting_t blockOn) {
  if (DebugThreadTransitions) {
    llvm::errs() << "[state id: " << id << "] Blocking thread " << thread.getThreadId().to_string() << " on ";
    std::visit([&os=llvm::errs()](auto&& w) {
      using T = std::decay_t<decltype(w)>;
      if constexpr (std::is_same_v<T, Thread::wait_none_t>) {
        os << "wait_none_t{}";
      } else if constexpr (std::is_same_v<T, Thread::wait_lock_t>) {
        os << "wait_lock_t{" << w.lock << "}";
      } else if constexpr (std::is_same_v<T, Thread::wait_cv_1_t>) {
        os << "wait_cv_1_t{" << w.cond << ", " << w.lock << "}";
      } else if constexpr (std::is_same_v<T, Thread::wait_cv_2_t>) {
        os << "wait_cv_2_t{" << w.cond << ", " << w.lock << "}";
      } else if constexpr (std::is_same_v<T, Thread::wait_join_t>) {
        os << "wait_join_t{" << w.thread.to_string() << "}";
      } else {
        assert(0);
      }
    }, blockOn);
    llvm::errs() << "\n";
  }

  assert(thread.state == ThreadState::Runnable || thread.state == ThreadState::Waiting);

  if (auto cv_2 = std::get_if<Thread::wait_cv_2_t>(&blockOn)) {
    assert(thread.state != ThreadState::Cutoff);
    assert(thread.state == ThreadState::Waiting);
    auto cv_1 = std::get<Thread::wait_cv_1_t>(thread.waiting);
    assert(cv_1.cond == cv_2->cond);
    assert(cv_1.lock == cv_2->lock);
  } else {
    assert(thread.state == ThreadState::Runnable);
  }

  thread.state = ThreadState::Waiting;
  thread.waiting = blockOn;

  needsThreadScheduling = true;
}

Thread::waiting_t ExecutionState::runThread(Thread &thread) {
  current = &thread;

  if (DebugThreadTransitions) {
    llvm::errs() << "[state id: " << id << "] Running thread " << current->getThreadId().to_string() << "\n";
  }

  const ThreadId &tid = thread.getThreadId();
  schedulingHistory.push_back(tid);
  currentSchedulingIndex = schedulingHistory.size() - 1;
  if (ptreeNode) {
    ptreeNode->schedulingDecision.scheduledThread = tid;
    ptreeNode->schedulingDecision.epochNumber = schedulingHistory.size();
  }

  Thread::waiting_t previous = Thread::wait_none_t{};

  if (thread.state == ThreadState::Waiting) {
    thread.state = ThreadState::Runnable;
    previous = thread.waiting;
    thread.waiting = Thread::wait_none_t{};
    std::visit([this,&tid](auto&& w) {
      using T = std::decay_t<decltype(w)>;
      if constexpr (std::is_same_v<T, Thread::wait_lock_t>) {
        memoryState.registerAcquiredLock(w.lock, tid);
      } else if constexpr (std::is_same_v<T, Thread::wait_cv_2_t>) {
        memoryState.registerAcquiredLock(w.lock, tid);
      }
    }, previous);
  } else if (thread.state != ThreadState::Runnable) {
    assert(0);
  }

  return previous;
}

std::set<ThreadId> ExecutionState::runnableThreads() {
  assert(porNode);
  auto cfg = porNode->configuration();

  std::set<ThreadId> runnable;
  for (auto &[tid, thread] : threads) {
    if (thread.isRunnable(cfg)) {
      bool overCsd = MaxContextSwitchDegree && por::is_above_csd_limit(*cfg.last_of_tid(tid), MaxContextSwitchDegree);
      bool isCutoff = cfg.last_of_tid(tid)->is_cutoff();
      if (isCutoff || overCsd) {
        if (!needsCatchUp()) {
          if (overCsd) {
            ++stats::csdThreads;
          } else if (isCutoff) {
            ++stats::cutoffThreads;
          }
          cutoffThread(thread);
        }
      } else {
        runnable.insert(tid);
      }
    }
  }
  return runnable;
}

void ExecutionState::addSymbolic(const MemoryObject *mo, const Array *array) {
  symbolics.emplace_back(std::make_pair(ref<const MemoryObject>(mo), array));
}

/**/

llvm::raw_ostream &klee::operator<<(llvm::raw_ostream &os, const MemoryMap &mm) {
  os << "{";
  MemoryMap::iterator it = mm.begin();
  MemoryMap::iterator ie = mm.end();
  if (it!=ie) {
    os << "MO" << it->first->id << ":" << it->second.get();
    for (++it; it!=ie; ++it)
      os << ", MO" << it->first->id << ":" << it->second.get();
  }
  os << "}";
  return os;
}

void ExecutionState::dumpSchedulingInfo(llvm::raw_ostream &out) const {
  for (auto &[tid, thread] : threads) {
    out << "[state id: " << id << " tid: " << tid.to_string() << "] ";
    if (thread.state == ThreadState::Waiting) {
      out << "blocked on ";
      std::visit([&out](auto&& w) {
        using T = std::decay_t<decltype(w)>;
        if constexpr (std::is_same_v<T, Thread::wait_none_t>) {
          out << "wait_none_t{}";
        } else if constexpr (std::is_same_v<T, Thread::wait_lock_t>) {
          out << "wait_lock_t{" << w.lock << "}";
        } else if constexpr (std::is_same_v<T, Thread::wait_cv_1_t>) {
          out << "wait_cv_1_t{" << w.cond << ", " << w.lock << "}";
        } else if constexpr (std::is_same_v<T, Thread::wait_cv_2_t>) {
          out << "wait_cv_2_t{" << w.cond << ", " << w.lock << "}";
        } else if constexpr (std::is_same_v<T, Thread::wait_join_t>) {
          out << "wait_join_t{" << w.thread.to_string() << "}";
        } else {
          assert(0 && "unknown waiting handle");
        }
      }, thread.waiting);
    } else if (thread.state == ThreadState::Runnable) {
      out << "runnable";
    } else if (thread.state == ThreadState::Cutoff) {
      out << "cutoff";
    } else if (thread.state == ThreadState::Exited) {
      out << "exited";
    } else {
      assert(0 && "unknown thread state");
    }
    out << "\n";
  }
}

void ExecutionState::performAllocatorFree(const MemoryObject* mo) {
  auto thread = getThreadById(mo->getAllocationStackFrame().first);
  assert(thread.has_value() && "MemoryObject created by thread that is not known");
  mo->parent->deallocate(mo, thread.value().get());
}

void ExecutionState::performAllocatorFree(const por::event::event &event) {
  // So we want to find all events (except from events that we triggered) and
  // check if there are any frees we have to perform

  if (event.kind() == por::event::event_kind::thread_init) {
    // So we only init the current thread, therefore, there cannot
    // be any memory objects that we had created before
    return;
  }

  for (const auto *pred : event.synchronized_events()) {
    auto& delayedFreesPerThread = pred->metadata().pending_frees;
    // Check if there is something for this thread
    auto it = delayedFreesPerThread.find(event.tid());
    if (it != delayedFreesPerThread.end()) {
      for (const auto* mo : it->second) {
        performAllocatorFree(mo);
      }
    }
  }
}

void ExecutionState::dumpStackOfThread(llvm::raw_ostream &out, const Thread* thread) const {
  unsigned idx = 0;

  const KInstruction *target = thread->prevPc;
  for (auto it = thread->stack.rbegin(), ie = thread->stack.rend(); it != ie; ++it) {
    const StackFrame &sf = *it;
    const InstructionInfo &ii = *target->info;
    out << "\t#" << idx++;
    std::stringstream AssStream;
    AssStream << std::setw(8) << std::setfill('0') << ii.assemblyLine;
    out << AssStream.str();
    out << " in ";
    DebugPrinter::printCall(out, sf.kf, sf);

    if (ii.file != "")
      out << " at " << ii.file << ":" << ii.line;
    out << "\n";
    target = sf.caller;
  }
}

void ExecutionState::dumpStack(llvm::raw_ostream &out) const {
  dumpStackOfThread(out, &thread());
}

void ExecutionState::dumpAllThreadStacks(llvm::raw_ostream &out) const {
  for (auto& threadIt : threads) {
    const Thread* thread = &threadIt.second;
    out << "Stacktrace of thread tid = " << thread->tid << ":\n";
    dumpStackOfThread(out, thread);
  }
}

std::size_t klee::klee_state_id(const ExecutionState *state) {
  return state->id;
}
