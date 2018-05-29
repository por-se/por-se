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
    threadSchedulingEnabled(1),

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

void ExecutionState::popFrameOfThread(Thread* thread) {
  // TODO: we should probably do this in the tread?
  // We want to unbind all the objects from the current tread frame
  StackFrame &sf = thread->stack.back();
  for (auto &it : sf.allocas) {
    addressSpace.unbindObject(it);
  }

  // Let the thread class handle the rest
  thread->popStackFrame();
}

void ExecutionState::popFrameOfCurrentThread() {
  Thread* thread = getCurrentThreadReference();
  popFrameOfThread(thread);
}

Thread* ExecutionState::createThread(Thread::ThreadId tid, KFunction *kf) {
  Thread thread = Thread(tid, kf);
  auto result = threads.insert(std::make_pair(tid, thread));
  assert(result.second);

  Thread* newThread = &result.first->second;
  newThread->synchronizationPoint = currentSynchronizationPoint + 1;
  newThread->state = Thread::ThreadState::PREEMPTED;

  // TODO: determine if we want the current thread to wait
  //       if we branch, then we should consider to preempt
  //       the current thread to determine the next schedule

  return newThread;
}

bool ExecutionState::moveToNewSyncPhase() {
  currentSynchronizationPoint++;
  bool oneRunnable = false;

  for (auto& threadIt : threads) {
    Thread* thread = &threadIt.second;

    if (thread->state == Thread::ThreadState::RUNNABLE) {
      oneRunnable = true;
    }

    if (thread->state == Thread::ThreadState::PREEMPTED) {
      thread->state = Thread::ThreadState::RUNNABLE;
      oneRunnable = true;
    }

    thread->synchronizationPoint = currentSynchronizationPoint;

    for (auto access : thread->syncPhaseAccesses) {
      const MemoryObject* mo = access.first;
      assert(mo->refCount > 0);
      mo->refCount--;
      if (mo->refCount == 0) {
        delete mo;
      }
    }

    thread->syncPhaseAccesses.clear();
  }

  return oneRunnable;
}

std::vector<Thread::ThreadId> ExecutionState::calculateRunnableThreads() {
  std::vector<Thread::ThreadId > runnableThreads;

  // First determine all that are runnable
  for (auto& threadIt : threads) {
    Thread* thread = &threadIt.second;

    if (thread->synchronizationPoint == currentSynchronizationPoint
        && thread->state == Thread::ThreadState::RUNNABLE) {
      runnableThreads.push_back(thread->getThreadId());
    }
  }

  return runnableThreads;
}

void ExecutionState::setCurrentScheduledThread(Thread::ThreadId tid) {
  auto threadIt = threads.find(tid);
  assert(threadIt != threads.end() && "Could not find thread");
  currentThreadIterator = threadIt;
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
  thread->state = thread->PREEMPTED;
  thread->synchronizationPoint++;
}

void ExecutionState::exitThread(Thread::ThreadId tid) {
  auto pair = threads.find(tid);
  assert(pair != threads.end() && "Could not find thread by id");

  Thread* thread = &pair->second;
  thread->state = thread->EXITED;
  thread->synchronizationPoint++;

  // Now remove all stack frames to cleanup
  // while (!thread->stack.empty()) {
  //  popFrameOfThread(thread);
  //}
}

void ExecutionState::trackMemoryAccess(const MemoryObject* mo, uint8_t type) {
  if (!threadSchedulingEnabled) {
    // We do not have any interference at the moment
    // so we do not need to record the current memory accesses
    return;
  }

  Thread* thread = getCurrentThreadReference();
  auto it = thread->syncPhaseAccesses.find(mo);

  if (it == thread->syncPhaseAccesses.end()) {
    thread->syncPhaseAccesses.insert(std::make_pair(mo, type));
    mo->refCount++;
  } else {
    uint8_t newType = it->second | type;
    it->second = newType;
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

bool ExecutionState::merge(const ExecutionState &b) {
  if (DebugLogStateMerge)
    llvm::errs() << "-- attempting merge of A:" << this << " with B:" << &b
                 << "--\n";

  Thread* curThread = getCurrentThreadReference();
  Thread* curThreadB = b.getCurrentThreadReference();

  if (curThread->pc != curThreadB->pc)
    return false;

  // XXX is it even possible for these to differ? does it matter? probably
  // implies difference in object states?
  if (symbolics!=b.symbolics)
    return false;

  {
    std::vector<StackFrame>::const_iterator itA = curThread->stack.begin();
    std::vector<StackFrame>::const_iterator itB = curThreadB->stack.begin();
    while (itA != curThread->stack.end() && itB != curThreadB->stack.end()) {
      // XXX vaargs?
      if (itA->caller != itB->caller || itA->kf != itB->kf)
        return false;
      ++itA;
      ++itB;
    }

    if (itA != curThread->stack.end() || itB != curThreadB->stack.end())
      return false;
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

  std::vector<StackFrame>::iterator itA = curThread->stack.begin();
  std::vector<StackFrame>::const_iterator itB = curThreadB->stack.begin();
  for (; itA != curThread->stack.end(); ++itA, ++itB) {
    StackFrame &af = *itA;
    const StackFrame &bf = *itB;
    for (unsigned i=0; i<af.kf->numRegisters; i++) {
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
