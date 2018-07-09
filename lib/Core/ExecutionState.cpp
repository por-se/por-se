//===-- ExecutionState.cpp ------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "klee/ExecutionState.h"

#include "klee/Internal/Module/Cell.h"
#include "klee/Internal/Module/InstructionInfoTable.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/KModule.h"

#include "klee/Expr.h"
#include "klee/Thread.h"

#include "Memory.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

#include <iomanip>
#include <sstream>
#include <cassert>
#include <map>
#include <set>
#include <stdarg.h>

using namespace llvm;
using namespace klee;

namespace { 
  cl::opt<bool>
  DebugLogStateMerge("debug-log-state-merge");
}

/***/

ExecutionState::ExecutionState(KFunction *kf) :
    currentSynchronizationPoint(0),
    threadSchedulingEnabled(true),
    liveThreadCount(0),
    justMovedToNewSyncPhase(true),

    queryCost(0.), 
    weight(1),
    depth(0),

    instsSinceCovNew(0),
    coveredNew(false),
    forkDisabled(false),
    ptreeNode(0),
    steppedInstructions(0) {

  // Thread 0 is always the main function thread
  Thread thread = Thread(0, kf);
  auto result = threads.insert(std::make_pair(thread.getThreadId(), thread));
  assert(result.second);
  currentThreadIterator = result.first;

  Thread* curThread = getCurrentThreadReference();
  curThread->state = Thread::ThreadState::RUNNABLE;
  curThread->synchronizationPoint = 0;
}

ExecutionState::ExecutionState(const std::vector<ref<Expr> > &assumptions)
    : constraints(assumptions), queryCost(0.), ptreeNode(0) {}

ExecutionState::~ExecutionState() {
  for (unsigned int i=0; i<symbolics.size(); i++)
  {
    const MemoryObject *mo = symbolics[i].first;
    assert(mo->refCount > 0);
    mo->refCount--;
    if (mo->refCount == 0)
      delete mo;
  }

  for (auto cur_mergehandler: openMergeStack){
    cur_mergehandler->removeOpenState(this);
  }

  // We have to clean up all stack frames of all threads
  for (auto& it : threads) {
    Thread thread = it.second;

    while (!thread.stack.empty()) {
      popFrameOfThread(&thread);
    }
  }
}

ExecutionState::ExecutionState(const ExecutionState& state):
    fnAliases(state.fnAliases),
    currentSynchronizationPoint(state.currentSynchronizationPoint),
    threads(state.threads),
    schedulingHistory(state.schedulingHistory),
    threadSchedulingEnabled(state.threadSchedulingEnabled),

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
    openMergeStack(state.openMergeStack),
    steppedInstructions(state.steppedInstructions)
{
  // Since we copied the threads, we can use the thread id to look it up
  Thread* curStateThread = state.getCurrentThreadReference();
  currentThreadIterator = threads.find(curStateThread->getThreadId());

  for (unsigned int i=0; i<symbolics.size(); i++)
    symbolics[i].first->refCount++;

  for (auto cur_mergehandler: openMergeStack)
    cur_mergehandler->addOpenState(this);
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

Thread* ExecutionState::getCurrentThreadReference() const {
  return &(currentThreadIterator->second);
}

std::vector<const MemoryObject *> ExecutionState::popFrameOfThread(Thread* thread) {
  // We want to unbind all the objects from the current tread frame
  StackFrame &sf = thread->stack.back();
  for (auto &it : sf.allocas) {
    addressSpace.unbindObject(it);
  }

  std::vector<const MemoryObject *> freedAllocas = sf.allocas;

  // Let the thread class handle the rest
  thread->popStackFrame();

  return freedAllocas;
}

std::vector<const MemoryObject *> ExecutionState::popFrameOfCurrentThread() {
  Thread* thread = getCurrentThreadReference();
  return popFrameOfThread(thread);
}

Thread* ExecutionState::createThread(Thread::ThreadId tid, KFunction *kf) {
  Thread thread = Thread(tid, kf);
  auto result = threads.insert(std::make_pair(tid, thread));
  assert(result.second);

  Thread* newThread = &result.first->second;
  newThread->synchronizationPoint = currentSynchronizationPoint + 1;
  newThread->state = Thread::ThreadState::PREEMPTED;

  // The newly spawned thread will be in sync will all the others
  // that are currently running so safe this info
  for (auto& threadIt : threads) {
    Thread::ThreadId itTid = threadIt.second.getThreadId();
    if (itTid == tid) {
      continue;
    }

    newThread->threadSyncs[itTid] = currentSynchronizationPoint;
    threadIt.second.threadSyncs[tid] = currentSynchronizationPoint;
  }

  // TODO: determine if we want the current thread to wait
  //       if we branch, then we should consider to preempt
  //       the current thread to determine the next schedule

  return newThread;
}

bool ExecutionState::moveToNewSyncPhase() {
  currentSynchronizationPoint++;
  unsigned countOfLiveThreads = 0;

  for (auto& threadIt : threads) {
    Thread* thread = &threadIt.second;

    if (thread->state == Thread::ThreadState::RUNNABLE) {
      countOfLiveThreads++;
    }

    if (thread->state == Thread::ThreadState::PREEMPTED) {
      thread->state = Thread::ThreadState::RUNNABLE;
      countOfLiveThreads++;
    }

    thread->synchronizationPoint = currentSynchronizationPoint;

    // Now we want to determine if we can delete certain memory accesses
    // based on the thread syncs

    // first we need to determine which sync point is currently the minimum
    uint64_t minSyncPoint = 0;
    for (auto& threadSync : thread->threadSyncs) {
      if (minSyncPoint == 0 || minSyncPoint > threadSync.second) {
        minSyncPoint = threadSync.second;
      }
    }

    // Now we can clear every access that happened before
    for (auto &access : thread->syncPhaseAccesses) {
      for (auto accessIt = access.second.begin(); accessIt != access.second.end(); accessIt++) {
        if (accessIt->syncPhase < minSyncPoint) {
          access.second.erase(accessIt);
        }
      }

      // If the access list is now empty, then we can remove the current
      // memory object reference
      if (access.second.empty()) {
        const MemoryObject *mo = access.first;
        assert(mo->refCount > 0);
        mo->refCount--;
        if (mo->refCount == 0) {
          delete mo;
        }

        thread->syncPhaseAccesses.erase(mo);
      }
    }

    thread->syncPhaseAccesses.clear();
  }

  liveThreadCount = countOfLiveThreads;

  return countOfLiveThreads > 0;
}

std::vector<Thread*> ExecutionState::calculateRunnableThreads() {
  std::vector<Thread*> runnableThreads;

  // First determine all that are runnable
  for (auto& threadIt : threads) {
    Thread* thread = &threadIt.second;

    if (thread->synchronizationPoint == currentSynchronizationPoint
        && thread->state == Thread::ThreadState::RUNNABLE) {
      runnableThreads.push_back(thread);
    }
  }

  return runnableThreads;
}

void ExecutionState::setCurrentScheduledThread(Thread::ThreadId tid) {
  auto threadIt = threads.find(tid);
  assert(threadIt != threads.end() && "Could not find thread");
  currentThreadIterator = threadIt;

  schedulingHistory.push_back(tid);
}

void ExecutionState::sleepCurrentThread() {
  Thread* thread = getCurrentThreadReference();
  thread->synchronizationPoint++;
  thread->state = Thread::ThreadState::SLEEPING;
}

void ExecutionState::preemptCurrentThread() {
  Thread* thread = getCurrentThreadReference();
  thread->synchronizationPoint++;
  thread->state = Thread::ThreadState::PREEMPTED;
}

void ExecutionState::wakeUpThread(Thread::ThreadId tid) {
  auto pair = threads.find(tid);
  assert(pair != threads.end() && "Could not find thread by id");

  Thread* thread = &pair->second;
  Thread* currentThread = getCurrentThreadReference();

  // We should only wake up threads that are actually sleeping
  if (thread->state == Thread::ThreadState::SLEEPING) {
    thread->state = Thread::ThreadState::PREEMPTED;
    thread->synchronizationPoint++;

    Thread::ThreadId curThreadId = currentThread->getThreadId();

    // One thread has woken up another one so make sure we remember that they
    // are at sync in this moment
    thread->threadSyncs[curThreadId] = currentSynchronizationPoint;
    currentThread->threadSyncs[tid] = currentSynchronizationPoint;

    // But since these threads are now in sync; we need to rebalance all other threads
    // as well, consider: if one thread has synced  with a third at a later state than
    // the other thread, then we know now for sure that the sync will be transitive:
    // We indirectly sync with the thread through the other one

    for (auto& threadSyncIt : thread->threadSyncs) {
      Thread::ThreadId thirdPartyTid = threadSyncIt.first;

      // We just synced them above so safely skip them
      if (thirdPartyTid == curThreadId) {
        continue;
      }

      uint64_t threadSyncedAt = threadSyncIt.second;
      uint64_t curThreadSyncedAt = currentThread->threadSyncs[thirdPartyTid];

      // Another safe skip as we are at the same state
      if (threadSyncedAt == curThreadSyncedAt) {
        continue;
      }

      auto thThreadPair = threads.find(thirdPartyTid);
      assert(thThreadPair != threads.end() && "Could not find referenced thread");
      Thread* thirdPartyThread = &(thThreadPair->second);

      // Now find the one that is more recent than the other and update the values
      if (threadSyncedAt < curThreadSyncedAt) {
        thread->threadSyncs[thirdPartyTid] = curThreadSyncedAt;
        thirdPartyThread->threadSyncs[tid] = curThreadSyncedAt;
      } else if (threadSyncedAt < curThreadSyncedAt) {
        currentThread->threadSyncs[thirdPartyTid] = threadSyncedAt;
        thirdPartyThread->threadSyncs[curThreadId] = threadSyncedAt;
      }
    }
  }
}

void ExecutionState::exitThread(Thread::ThreadId tid) {
  auto pair = threads.find(tid);
  assert(pair != threads.end() && "Could not find thread by id");

  Thread* thread = &pair->second;
  thread->state = thread->EXITED;
  thread->synchronizationPoint++;

  Thread* currentThread = getCurrentThreadReference();
  if (currentThread->getThreadId() != thread->getThreadId()) {
    thread->threadSyncs[currentThread->getThreadId()] = currentSynchronizationPoint;
    currentThread->threadSyncs[thread->getThreadId()] = currentSynchronizationPoint;
  }

  // Now remove all stack frames to cleanup
  // while (!thread->stack.empty()) {
  //  popFrameOfThread(thread);
  //}
}

void ExecutionState::trackMemoryAccess(const MemoryObject* mo, ref<Expr> offset, uint8_t type) {
  if (!threadSchedulingEnabled) {
    // We do not have any interference at the moment
    // so we do not need to record the current memory accesses
    return;
  }

  Thread* thread = getCurrentThreadReference();
  bool trackedNewMo = thread->trackMemoryAccess(mo, offset, type);
  if (trackedNewMo) {
    mo->refCount++;
  }
}

void ExecutionState::addSymbolic(const MemoryObject *mo, const Array *array) { 
  mo->refCount++;
  symbolics.push_back(std::make_pair(mo, array));
}
///

std::string ExecutionState::getFnAlias(std::string fn) {
  std::map < std::string, std::string >::iterator it = fnAliases.find(fn);
  if (it != fnAliases.end())
    return it->second;
  else return "";
}

void ExecutionState::addFnAlias(std::string old_fn, std::string new_fn) {
  fnAliases[old_fn] = new_fn;
}

void ExecutionState::removeFnAlias(std::string fn) {
  fnAliases.erase(fn);
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

bool ExecutionState::hasSameThreadState(const ExecutionState &b, Thread::ThreadId tid) {
  auto threadA = threads.find(tid);
  auto threadB = b.threads.find(tid);

  if ((threadA != threads.end()) ^ (threadB != b.threads.end())) {
    // This means at least one of the states does not have it and the other had the thread
    return false;
  }

  if (threadA == threads.end()) {
    // No such thread exists. So probably right to return false?
    return false;
  }

  Thread* curThreadA = &(threadA->second);
  const Thread* curThreadB = &(threadB->second);

  if (curThreadA->state != curThreadB->state) {
    return false;
  }

  if (curThreadA->pc != curThreadB->pc) {
    return false;
  }

  // TODO: should we compare at which sync point we are actually? And what about memory accesses

  std::vector<StackFrame>::const_iterator itA = curThreadA->stack.begin();
  std::vector<StackFrame>::const_iterator itB = curThreadB->stack.begin();
  while (itA != curThreadA->stack.end() && itB != curThreadB->stack.end()) {
    // XXX vaargs?
    if (itA->caller != itB->caller || itA->kf != itB->kf)
      return false;
    ++itA;
    ++itB;
  }

  return !(itA != curThreadA->stack.end() || itB != curThreadB->stack.end());
}

bool ExecutionState::merge(const ExecutionState &b) {
  if (DebugLogStateMerge)
    llvm::errs() << "-- attempting merge of A:" << this << " with B:" << &b
                 << "--\n";

  // XXX is it even possible for these to differ? does it matter? probably
  // implies difference in object states?
  if (symbolics!=b.symbolics)
    return false;

  if (threads.size() != b.threads.size()) {
    return false;
  }

  for (auto threadsIt : threads) {
    Thread::ThreadId tid = threadsIt.first;

    if (!hasSameThreadState(b, tid)) {
      return false;
    }
  }

  std::set< ref<Expr> > aConstraints(constraints.begin(), constraints.end());
  std::set< ref<Expr> > bConstraints(b.constraints.begin(), 
                                     b.constraints.end());
  std::set< ref<Expr> > commonConstraints, aSuffix, bSuffix;
  std::set_intersection(aConstraints.begin(), aConstraints.end(),
                        bConstraints.begin(), bConstraints.end(),
                        std::inserter(commonConstraints, commonConstraints.begin()));
  std::set_difference(aConstraints.begin(), aConstraints.end(),
                      commonConstraints.begin(), commonConstraints.end(),
                      std::inserter(aSuffix, aSuffix.end()));
  std::set_difference(bConstraints.begin(), bConstraints.end(),
                      commonConstraints.begin(), commonConstraints.end(),
                      std::inserter(bSuffix, bSuffix.end()));
  if (DebugLogStateMerge) {
    llvm::errs() << "\tconstraint prefix: [";
    for (std::set<ref<Expr> >::iterator it = commonConstraints.begin(),
                                        ie = commonConstraints.end();
         it != ie; ++it)
      llvm::errs() << *it << ", ";
    llvm::errs() << "]\n";
    llvm::errs() << "\tA suffix: [";
    for (std::set<ref<Expr> >::iterator it = aSuffix.begin(),
                                        ie = aSuffix.end();
         it != ie; ++it)
      llvm::errs() << *it << ", ";
    llvm::errs() << "]\n";
    llvm::errs() << "\tB suffix: [";
    for (std::set<ref<Expr> >::iterator it = bSuffix.begin(),
                                        ie = bSuffix.end();
         it != ie; ++it)
      llvm::errs() << *it << ", ";
    llvm::errs() << "]\n";
  }

  // We cannot merge if addresses would resolve differently in the
  // states. This means:
  // 
  // 1. Any objects created since the branch in either object must
  // have been free'd.
  //
  // 2. We cannot have free'd any pre-existing object in one state
  // and not the other

  if (DebugLogStateMerge) {
    llvm::errs() << "\tchecking object states\n";
    llvm::errs() << "A: " << addressSpace.objects << "\n";
    llvm::errs() << "B: " << b.addressSpace.objects << "\n";
  }
    
  std::set<const MemoryObject*> mutated;
  MemoryMap::iterator ai = addressSpace.objects.begin();
  MemoryMap::iterator bi = b.addressSpace.objects.begin();
  MemoryMap::iterator ae = addressSpace.objects.end();
  MemoryMap::iterator be = b.addressSpace.objects.end();
  for (; ai!=ae && bi!=be; ++ai, ++bi) {
    if (ai->first != bi->first) {
      if (DebugLogStateMerge) {
        if (ai->first < bi->first) {
          llvm::errs() << "\t\tB misses binding for: " << ai->first->id << "\n";
        } else {
          llvm::errs() << "\t\tA misses binding for: " << bi->first->id << "\n";
        }
      }
      return false;
    }
    if (ai->second != bi->second) {
      if (DebugLogStateMerge)
        llvm::errs() << "\t\tmutated: " << ai->first->id << "\n";
      mutated.insert(ai->first);
    }
  }
  if (ai!=ae || bi!=be) {
    if (DebugLogStateMerge)
      llvm::errs() << "\t\tmappings differ\n";
    return false;
  }
  
  // merge stack

  ref<Expr> inA = ConstantExpr::alloc(1, Expr::Bool);
  ref<Expr> inB = ConstantExpr::alloc(1, Expr::Bool);
  for (std::set< ref<Expr> >::iterator it = aSuffix.begin(), 
         ie = aSuffix.end(); it != ie; ++it)
    inA = AndExpr::create(inA, *it);
  for (std::set< ref<Expr> >::iterator it = bSuffix.begin(), 
         ie = bSuffix.end(); it != ie; ++it)
    inB = AndExpr::create(inB, *it);

  // XXX should we have a preference as to which predicate to use?
  // it seems like it can make a difference, even though logically
  // they must contradict each other and so inA => !inB

  for (auto& threadPair : threads) {
    auto bThread = b.threads.find(threadPair.first);
    if (bThread == b.threads.end()) {
      return false;
    }

    Thread* threadOfA = &(threadPair.second);
    const Thread* threadOfB = &(bThread->second);

    auto itA = threadOfA->stack.begin();
    auto itB = threadOfB->stack.begin();
    for (; itA != threadOfA->stack.end(); ++itA, ++itB) {
      StackFrame &af = *itA;
      const StackFrame &bf = *itB;
      for (unsigned i = 0; i < af.kf->numRegisters; i++) {
        ref<Expr> &av = af.locals[i].value;
        const ref<Expr> &bv = bf.locals[i].value;
        if (av.isNull() || bv.isNull()) {
          // if one is null then by implication (we are at same pc)
          // we cannot reuse this local, so just ignore
        } else {
          av = SelectExpr::create(inA, av, bv);
        }
      }
    }
  }

  for (std::set<const MemoryObject*>::iterator it = mutated.begin(), 
         ie = mutated.end(); it != ie; ++it) {
    const MemoryObject *mo = *it;
    const ObjectState *os = addressSpace.findObject(mo);
    const ObjectState *otherOS = b.addressSpace.findObject(mo);
    assert(os && !os->readOnly && 
           "objects mutated but not writable in merging state");
    assert(otherOS);

    ObjectState *wos = addressSpace.getWriteable(mo, os);
    for (unsigned i=0; i<mo->size; i++) {
      ref<Expr> av = wos->read8(i);
      ref<Expr> bv = otherOS->read8(i);
      wos->write(i, SelectExpr::create(inA, av, bv));
    }
  }

  constraints = ConstraintManager();
  for (std::set< ref<Expr> >::iterator it = commonConstraints.begin(), 
         ie = commonConstraints.end(); it != ie; ++it)
    constraints.addConstraint(*it);
  constraints.addConstraint(OrExpr::create(inA, inB));

  return true;
}

void ExecutionState::dumpSchedulingInfo(llvm::raw_ostream &out) const {
  out << "Thread scheduling:\n";
  for (const auto& threadId : threads) {
    const Thread* thread = &threadId.second;

    std::string stateName;
    if (thread->state == 0) {
      stateName = "preempted";
    } else if (thread->state == 1) {
      stateName = "sleeping";
    } else if (thread->state == 2) {
      stateName = "runnable";
    } else if (thread->state == 3) {
      stateName = "exited";
    } else {
      stateName = "unknown";
    }

    out << "Tid: " << thread->tid << " in state: " << stateName
        << " at point: " << thread->synchronizationPoint << "\n";
  }
}

void ExecutionState::dumpStackOfThread(llvm::raw_ostream &out, const Thread* thread) const {
  unsigned idx = 0;

  const KInstruction *target = thread->prevPc;
  for (ExecutionState::stack_ty::const_reverse_iterator
         it = thread->stack.rbegin(), ie = thread->stack.rend();
       it != ie; ++it) {
    const StackFrame &sf = *it;
    Function *f = sf.kf->function;
    const InstructionInfo &ii = *target->info;
    out << "\t#" << idx++;
    std::stringstream AssStream;
    AssStream << std::setw(8) << std::setfill('0') << ii.assemblyLine;
    out << AssStream.str();
    out << " in " << f->getName().str() << " (";
    // Yawn, we could go up and print varargs if we wanted to.
    unsigned index = 0;
    for (Function::arg_iterator ai = f->arg_begin(), ae = f->arg_end();
         ai != ae; ++ai) {
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
  const Thread* thread = getCurrentThreadReference();
  dumpStackOfThread(out, thread);
}

void ExecutionState::dumpAllThreadStacks(llvm::raw_ostream &out) const {
  for (auto& threadIt : threads) {
    const Thread* thread = &threadIt.second;
    out << "Stacktrace of thread tid = " << thread->tid << ":\n";
    dumpStackOfThread(out, thread);
  }
}
