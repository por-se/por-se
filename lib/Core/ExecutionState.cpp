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
#include "klee/OptionCategories.h"
#include "klee/Thread.h"

#include "Memory.h"
#include "MemoryState.h"
#include "MemoryManager.h"
#include "PTree.h"

#include "llvm/IR/Function.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

#include "por/configuration.h"

#include <algorithm>
#include <cassert>
#include <cstdarg>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>

using namespace klee;

size_t ExecutionState::next_id = 0;
const ThreadId ExecutionState::mainThreadId{ThreadId(), 1};

/***/

ExecutionState::ExecutionState(KFunction *kf) :
    id(next_id++),
    currentSchedulingIndex(0),
    onlyOneThreadRunnableSinceEpochStart(true),
    threadSchedulingEnabled(true),
    atomicPhase(false),
    weight(1),
    depth(0),
    instsSinceCovNew(0),
    coveredNew(false),
    forkDisabled(false),
    ptreeNode(0),
    memoryState(this),
    steppedInstructions(0)
{
    auto result = threads.emplace(std::piecewise_construct,
                                  std::forward_as_tuple(mainThreadId),
                                  std::forward_as_tuple(mainThreadId, kf));
    assert(result.second);
    currentThread(result.first->second);
    runnableThreads.insert(mainThreadId);
    scheduleNextThread(mainThreadId);
}


ExecutionState::ExecutionState(const std::vector<ref<Expr> > &assumptions)
    : id(next_id++), constraints(assumptions), ptreeNode(nullptr),
      memoryState(this) {}

ExecutionState::~ExecutionState() {
  for (unsigned int i=0; i<symbolics.size(); i++)
  {
    const MemoryObject *mo = symbolics[i].first;
    assert(mo->refCount > 0);
    mo->refCount--;
    if (mo->refCount == 0)
      delete mo;
  }

  // We have to clean up all stack frames of all threads
  for (auto& [_, thread] : threads) {
    while (!thread.stack.empty()) {
      popFrameOfThread(&thread);
    }
  }
}

ExecutionState::ExecutionState(const ExecutionState& state):
    id(next_id++),
    lostNotifications(state.lostNotifications),
    currentSchedulingIndex(state.currentSchedulingIndex),
    onlyOneThreadRunnableSinceEpochStart(state.onlyOneThreadRunnableSinceEpochStart),
    raceDetection(state.raceDetection),
    threads(state.threads),
    schedulingHistory(state.schedulingHistory),
    runnableThreads(state.runnableThreads),
    threadSchedulingEnabled(state.threadSchedulingEnabled),
    atomicPhase(state.atomicPhase),

    addressSpace(state.addressSpace),
    constraints(state.constraints),

    queryCost(state.queryCost),
    weight(state.weight),
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
    steppedInstructions(state.steppedInstructions)
{
  // Since we copied the threads, we can use the thread id to look it up
  currentThread(state.currentThreadId());

  for (unsigned int i=0; i<symbolics.size(); i++)
    symbolics[i].first->refCount++;
}

ExecutionState *ExecutionState::branch() {
  depth++;

  ExecutionState *falseState = new ExecutionState(*this);
  falseState->coveredNew = false;
  falseState->coveredLines.clear();

  weight *= .5;
  falseState->weight -= weight;

  return falseState;
}

void ExecutionState::popFrameOfThread(Thread* thread) {
  StackFrame &sf = thread->stack.back();

  for (auto it = sf.allocas.rbegin(), end = sf.allocas.rend(); it != end; it++) {
    const MemoryObject* mo = *it;

    mo->parent->deallocate(mo, *thread);
    addressSpace.unbindObject(mo);
  }

  if (PruneStates) {
    memoryState.registerPopFrame(sf);
  }

  // Let the thread class handle the rest
  thread->popStackFrame();
}

void ExecutionState::popFrameOfCurrentThread() {
  popFrameOfThread(&currentThread());
}

Thread &ExecutionState::createThread(KFunction *kf, ref<Expr> runtimeStructPtr) {
  auto& curThread = currentThread();

  ThreadId tid = ThreadId(curThread.tid, ++curThread.spawnedThreads);

  auto result = threads.emplace(std::piecewise_construct,
                                std::forward_as_tuple(tid),
                                std::forward_as_tuple(tid, kf));
  assert(result.second);

  Thread &newThread = result.first->second;
  newThread.runtimeStructPtr = runtimeStructPtr;

  // New threads are by default directly runnable
  runnableThreads.insert(tid);

  return newThread;
}

void ExecutionState::scheduleNextThread(const ThreadId &tid) {
  Thread &thread = currentThread(tid);

  assert(thread.state == ThreadState::Runnable && "Cannot schedule non-runnable thread");
  assert(thread.waitingHandle == 0 && "Thread may not be waiting on a resource");

  schedulingHistory.push_back(tid);
  if (ptreeNode) {
    ptreeNode->schedulingDecision.scheduledThread = tid;
    ptreeNode->schedulingDecision.epochNumber = schedulingHistory.size();
  }

  // So it can happen that this is the first execution of the thread since it was waiting
  // so we might have to disable thread scheduling again
  if (thread.threadSchedulingWasDisabled) {
    thread.threadSchedulingWasDisabled = false;
    threadSchedulingEnabled = false;
  }

  currentSchedulingIndex = schedulingHistory.size() - 1;
  onlyOneThreadRunnableSinceEpochStart = runnableThreads.size() == 1;
}

void ExecutionState::threadWaitOn(std::uint64_t lid) {
  Thread &thread = currentThread();
  assert(thread.waitingHandle == 0 && "Thread should not be waiting on another resource");

  thread.state = ThreadState::Waiting;

  // If a thread goes into the waiting state when it had deactivated thread scheduling,
  // then we will safe this and will reenable thread scheduling for as long as this thread
  // is not running again
  thread.threadSchedulingWasDisabled = !threadSchedulingEnabled;
  threadSchedulingEnabled = true;

  runnableThreads.erase(thread.getThreadId());

  thread.waitingHandle = lid;
}

void ExecutionState::preemptThread(const ThreadId &tid) {
  auto thread = getThreadById(tid);
  assert(thread && "Could not find thread with given id");

  thread->get().state = ThreadState::Runnable;
  runnableThreads.insert(tid);
}

void ExecutionState::wakeUpThread(const ThreadId &tid) {
  auto thread = getThreadById(tid);
  assert(thread && "Could not find thread with given id");

  runnableThreads.insert(tid);

  // We should only wake up threads that are actually waiting
  if (thread->get().state == ThreadState::Waiting) {
    thread->get().state = ThreadState::Runnable;

    thread->get().waitingHandle = 0;
  }
}

void ExecutionState::exitThread(const ThreadId &tid) {
  auto thread = getThreadById(tid);
  assert(thread && "Could not find thread with given id");

  thread->get().state = ThreadState::Exited;
  runnableThreads.erase(tid);

   // Now remove all stack frames
   while (!thread->get().stack.empty()) {
    popFrameOfThread(&thread->get());
  }
}

void ExecutionState::addSymbolic(const MemoryObject *mo, const Array *array) { 
  mo->refCount++;
  symbolics.emplace_back(mo, array);
}

/**/

llvm::raw_ostream &klee::operator<<(llvm::raw_ostream &os, const MemoryMap &mm) {
  os << "{";
  MemoryMap::iterator it = mm.begin();
  MemoryMap::iterator ie = mm.end();
  if (it!=ie) {
    os << "MO" << it->first->id << ":" << it->second;
    for (++it; it!=ie; ++it)
      os << ", MO" << it->first->id << ":" << it->second;
  }
  os << "}";
  return os;
}

void ExecutionState::dumpSchedulingInfo(llvm::raw_ostream &out) const {
  out << "Thread scheduling:\n";
  for (const auto& threadId : threads) {
    const Thread* thread = &threadId.second;

    std::string stateName;
    if (thread->state == ThreadState::Waiting) {
      stateName = "waiting";
    } else if (thread->state == ThreadState::Runnable) {
      stateName = "runnable";
    } else if (thread->state == ThreadState::Exited) {
      stateName = "exited";
    } else {
      stateName = "unknown";
      assert(0 && "ThreadState value not defined!");
    }

    out << "Tid: " << thread->tid << " in state: " << stateName << "\n";
  }
}

void ExecutionState::dumpStackOfThread(llvm::raw_ostream &out, const Thread* thread) const {
  unsigned idx = 0;

  const KInstruction *target = thread->prevPc;
  for (auto it = thread->stack.rbegin(), ie = thread->stack.rend(); it != ie; ++it) {
    const StackFrame &sf = *it;
    llvm::Function *f = sf.kf->function;
    const InstructionInfo &ii = *target->info;
    out << "\t#" << idx++;
    std::stringstream AssStream;
    AssStream << std::setw(8) << std::setfill('0') << ii.assemblyLine;
    out << AssStream.str();
    out << " in " << f->getName().str() << " (";
    // Yawn, we could go up and print varargs if we wanted to.
    unsigned index = 0;
    for (auto ai = f->arg_begin(), ae = f->arg_end(); ai != ae; ++ai) {
      if (ai!=f->arg_begin()) out << ", ";

      out << ai->getName().str();
      // XXX should go through function
      ref<Expr> value = sf.locals[sf.kf->getArgRegister(index++)].value;
      if (value.get() && isa<ConstantExpr>(value))
        out << "=" << value;
    }
    out << ")";
    if (ii.file != "")
      out << " at " << ii.file << ":" << ii.line;
    out << "\n";
    target = sf.caller;
  }
}

void ExecutionState::dumpStack(llvm::raw_ostream &out) const {
  dumpStackOfThread(out, &currentThread());
}

void ExecutionState::dumpAllThreadStacks(llvm::raw_ostream &out) const {
  for (auto& threadIt : threads) {
    const Thread* thread = &threadIt.second;
    out << "Stacktrace of thread tid = " << thread->tid << ":\n";
    dumpStackOfThread(out, thread);
  }
}

void ExecutionState::printFingerprint() const {
  auto current = memoryState.getFingerprint();
  llvm::errs() << "Current Fingerprint: " << MemoryFingerprint::toString(current) << "\n";

  auto global = memoryState.getGlobalFingerprintValue();
  llvm::errs() << "Global: " << MemoryFingerprint::toString(global) << "\n";

  for (auto &t : threads) {
    const Thread &thread = t.second;
    for (std::size_t i = 0; i < thread.stack.size(); ++i) {
      auto delta = thread.stack[i].fingerprintDelta;
      bool isCurrent = (currentThreadId() == t.first) && (thread.stack.size() - 1 == i);
      llvm::errs() << "Thread " << t.first << ":" << i
                   << (isCurrent ? " (current)" : "")
                   << " Delta: " << MemoryFingerprint::toString(delta) << "\n";
    }
  }
}

