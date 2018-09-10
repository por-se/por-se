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
#include "MemoryState.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

#include "MemoryAccessTracker.h"

#include <cryptopp/sha.h>

#include <algorithm>
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

size_t ExecutionState::next_id = 0;

/***/

ExecutionState::ExecutionState(KFunction *kf) :
    id(next_id++),
    currentSchedulingIndex(0),
    onlyOneThreadRunnableSinceEpochStart(true),
    completedScheduleCount(0),
    threadSchedulingEnabled(true),
    atomicPhase(false),
    queryCost(0.), 
    weight(1),
    depth(0),
    instsSinceCovNew(0),
    coveredNew(false),
    forkDisabled(false),
    ptreeNode(0),
    memoryState(this),
    steppedInstructions(0) {
  memAccessTracker = new MemoryAccessTracker();

  // Thread 0 is always the main function thread
  Thread thread = Thread(0, kf);
  auto result = threads.insert(std::make_pair(thread.getThreadId(), thread));
  assert(result.second);
  currentThreadIterator = result.first;

  Thread* curThread = getCurrentThreadReference();
  curThread->state = Thread::ThreadState::RUNNABLE;
  curThread->tid = 0;
  runnableThreads.insert(curThread->getThreadId());

  scheduleNextThread(curThread->getThreadId());
}

ExecutionState::ExecutionState(const std::vector<ref<Expr> > &assumptions)
    : id(next_id++), memAccessTracker(nullptr), constraints(assumptions),
      queryCost(0.), ptreeNode(0), memoryState(this) {}

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

  // Since we are the owner of the memory access tracker we can safely delete it
  delete memAccessTracker;
  memAccessTracker = nullptr;

  // We have to clean up all stack frames of all threads
  for (auto& it : threads) {
    Thread thread = it.second;

    while (!thread.stack.empty()) {
      popFrameOfThread(&thread);
    }
  }
}

ExecutionState::ExecutionState(const ExecutionState& state):
    id(next_id++),
    fnAliases(state.fnAliases),
    currentSchedulingIndex(state.currentSchedulingIndex),
    onlyOneThreadRunnableSinceEpochStart(state.onlyOneThreadRunnableSinceEpochStart),
    forwardDeclaredDependencies(state.forwardDeclaredDependencies),
    completedScheduleCount(state.completedScheduleCount),
    threads(state.threads),
    schedulingHistory(state.schedulingHistory),
    runnableThreads(state.runnableThreads),
    threadSchedulingEnabled(state.threadSchedulingEnabled),
    atomicPhase(state.atomicPhase),
    scheduleDependencies(state.scheduleDependencies),

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
    openMergeStack(state.openMergeStack),
    steppedInstructions(state.steppedInstructions)
{
  // So make sure that we also make a correct copy of the mem access tracker
  memAccessTracker = new MemoryAccessTracker(*state.memAccessTracker);

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

Thread* ExecutionState::getThreadReferenceById(Thread::ThreadId tid) {
  auto threadIt = threads.find(tid);
  if (threadIt == threads.end()) {
    return nullptr;
  }

  return &(threadIt->second);
}

ExecutionState::EpochDependencies* ExecutionState::getCurrentEpochDependencies() {
  return &(scheduleDependencies[getCurrentThreadReference()->getThreadId()].back());
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

Thread* ExecutionState::createThread(KFunction *kf, ref<Expr> arg) {
  Thread::ThreadId tid = threads.size();
  Thread thread = Thread(tid, kf);
  auto result = threads.insert(std::make_pair(tid, thread));
  assert(result.second);

  Thread* newThread = &result.first->second;
  newThread->startArg = arg;

  // New threads are by default directly runnable
  runnableThreads.insert(newThread->getThreadId());
  newThread->state = Thread::RUNNABLE;

  // We cannot sync the current thread with the others as the others since we can not
  // infer any knowledge from them
  Thread::ThreadId curTid = getCurrentThreadReference()->getThreadId();
  if (memAccessTracker != nullptr) {
    memAccessTracker->registerThreadDependency(tid, curTid, currentSchedulingIndex);
  }

  ScheduleDependency dep{};
  dep.scheduleIndex = currentSchedulingIndex;
  dep.tid = curTid;
  dep.reason = THREAD_CREATION;
  forwardDeclaredDependencies.insert(std::make_pair(newThread->getThreadId(), dep));

  return newThread;
}

void ExecutionState::assembleDependencyIndicator() {
  Thread* curThread = getCurrentThreadReference();
  uint64_t threadId = curThread->getThreadId();

  std::set<uint64_t> dependencySet;

  // First of all make pairs of all dependencies we have collected so far
  EpochDependencies* deps = getCurrentEpochDependencies();

  for (auto& dep : deps->dependencies) {
    dependencySet.insert(schedulingHistory[dep.scheduleIndex].dependencyHash);
  }

  CryptoPP::SHA256 finalHash;

  // First part of the hash: the hashes of all dependencies
  for (uint64_t hash : dependencySet) {
    unsigned char input[sizeof(hash)];
    std::memcpy(input, &hash, sizeof(hash));
    finalHash.Update(input, sizeof(hash));
  }

  // Now add our own identifier as the final part
  unsigned char identifier[sizeof(uint64_t) + sizeof(uint64_t)];
  uint64_t curCount = curThread->epochRunCount;
  std::memcpy(identifier, &threadId, sizeof(threadId));
  std::memcpy(&identifier[sizeof(threadId)], &curCount, sizeof(curCount));

  finalHash.Update(identifier, sizeof(identifier));

  // Now get our hash and save it into the corresponding fields
  uint8_t digest[CryptoPP::SHA256::DIGESTSIZE];
  finalHash.Final(digest);

  uint64_t ourHash = 0;
  std::memcpy(&ourHash, digest, sizeof(ourHash));

  deps->hash = ourHash;
  schedulingHistory.back().dependencyHash = ourHash;
}

void ExecutionState::endCurrentEpoch() {
  assembleDependencyIndicator();
  completedScheduleCount = schedulingHistory.size();
}

void ExecutionState::scheduleNextThread(Thread::ThreadId tid) {
  auto threadIt = threads.find(tid);
  assert(threadIt != threads.end() && "Could not find thread");
  currentThreadIterator = threadIt;

  assert(threadIt->second.state == Thread::RUNNABLE && "Trying to schedule a non runnable thread");

  ScheduleEpoch ep {};
  ep.tid = tid;
  schedulingHistory.push_back(ep);

  if (memAccessTracker != nullptr) {
    memAccessTracker->scheduledNewThread(tid);
  }

  // So it can happen that this is the first execution of the thread since it was going to sleep
  // so we might have to disable thread scheduling again
  if (threadIt->second.threadSchedulingWasDisabled) {
    threadIt->second.threadSchedulingWasDisabled = false;
    threadSchedulingEnabled = false;
  }

  currentSchedulingIndex = schedulingHistory.size() - 1;
  scheduleDependencies[tid].push_back(EpochDependencies());

  // Also add the reference to the last execution of our own thread
  if (threadIt->second.epochRunCount > 0) {
    uint64_t j = currentSchedulingIndex - 1;
    for (auto i = schedulingHistory.rbegin() + 1; i != schedulingHistory.rend(); ++i, --j) {
      if (i->tid == tid) {
        ScheduleDependency dep {};
        dep.reason = PREDECESSOR;
        dep.tid = tid;
        dep.scheduleIndex = j;

        trackScheduleDependency(dep);
        break;
      }
    }
  }

  threadIt->second.epochRunCount++;

  onlyOneThreadRunnableSinceEpochStart = runnableThreads.size() == 1;

  // Check if there are any other already declared dependencies that we might be missing
  auto tId = forwardDeclaredDependencies.find(tid);
  if (tId != forwardDeclaredDependencies.end()) {
    auto dep = tId->second;
    trackScheduleDependency(dep);

    // We declared everything so clear it
    forwardDeclaredDependencies.erase(tId);
  }
}

void ExecutionState::sleepCurrentThread() {
  Thread* thread = getCurrentThreadReference();
  thread->state = Thread::ThreadState::SLEEPING;

  // If a thread goes to sleep when it had deactivated thread scheduling,
  // then we will safe this and will reenable thread scheduling for as long as this thread
  // is not running again
  thread->threadSchedulingWasDisabled = !threadSchedulingEnabled;
  threadSchedulingEnabled = true;

  runnableThreads.erase(thread->getThreadId());
}

void ExecutionState::preemptThread(Thread::ThreadId tid) {
  auto pair = threads.find(tid);
  assert(pair != threads.end() && "Could not find thread by id");

  Thread* thread = &pair->second;
  thread->state = Thread::RUNNABLE;

  runnableThreads.insert(tid);
}

void ExecutionState::wakeUpThread(Thread::ThreadId tid) {
  auto pair = threads.find(tid);
  assert(pair != threads.end() && "Could not find thread by id");

  Thread* thread = &pair->second;
  Thread* currentThread = getCurrentThreadReference();

  runnableThreads.insert(thread->getThreadId());

  // We should only wake up threads that are actually sleeping
  if (thread->state == Thread::ThreadState::SLEEPING) {
    thread->state = Thread::RUNNABLE;

    Thread::ThreadId curThreadId = currentThread->getThreadId();

    // One thread has woken up another one so make sure we remember that they
    // are at sync in this moment
    if (memAccessTracker != nullptr) {
      memAccessTracker->registerThreadDependency(tid, curThreadId, currentSchedulingIndex);
    }

    ScheduleDependency dep{};
    dep.scheduleIndex = currentSchedulingIndex;
    dep.tid = curThreadId;
    dep.reason = THREAD_WAKEUP;
    forwardDeclaredDependencies.insert(std::make_pair(tid, dep));
  }
}

void ExecutionState::exitThread(Thread::ThreadId tid) {
  auto pair = threads.find(tid);
  assert(pair != threads.end() && "Could not find thread by id");

  Thread* thread = &pair->second;
  thread->state = thread->EXITED;
  runnableThreads.erase(thread->getThreadId());

  Thread* currentThread = getCurrentThreadReference();
  if (currentThread->getThreadId() != thread->getThreadId() && memAccessTracker != nullptr) {
    memAccessTracker->registerThreadDependency(tid, currentThread->tid, currentSchedulingIndex);
  }

   // Now remove all stack frames except the last one, because otherwise the stats tracker may fail
   while (thread->stack.size() > 1) {
    popFrameOfThread(thread);
  }
}

void ExecutionState::trackMemoryAccess(const MemoryObject* mo, ref<Expr> offset, uint8_t type) {
  // We do not need to track the memory access for now
  if (!onlyOneThreadRunnableSinceEpochStart && memAccessTracker != nullptr) {
    MemoryAccess access;
    access.type = type;
    access.offset = offset;
    access.atomicMemoryAccess = atomicPhase;
    access.safeMemoryAccess = !threadSchedulingEnabled || atomicPhase;

    memAccessTracker->trackMemoryAccess(mo->getId(), access);
  }
}

void ExecutionState::trackScheduleDependency(uint64_t scheduleIndex, Thread::ThreadId tid, DependencyReason r) {
  // so we only have the information about when something has happened
  // but now how often the referenced thread did execute

  ScheduleDependency d{};
  d.scheduleIndex = scheduleIndex;
  d.tid = tid;
  d.reason = r;

  trackScheduleDependency(d);
}

void ExecutionState::trackScheduleDependency(ScheduleDependency d) {
  assert(d.reason != 0 && "There has to be a reason");
  assert(d.scheduleIndex < currentSchedulingIndex && "The references have to point to the past");

  EpochDependencies* dep = getCurrentEpochDependencies();

  for (auto& e : dep->dependencies) {
    // Make sure that we actually have not already added this info
    if (e.scheduleIndex == d.scheduleIndex && e.tid == d.tid) {
      e.reason |= d.reason;
      return;
    }
  }

  Thread::ThreadId tid = getCurrentThreadReference()->getThreadId();
  if (d.tid != tid && (d.reason & ~ATOMIC_MEMORY_ACCESS) != 0 && memAccessTracker != nullptr) {
    memAccessTracker->registerThreadDependency(tid, d.tid, d.scheduleIndex);
  }

  dep->dependencies.push_back(d);
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
    if (thread->state == Thread::ThreadState::SLEEPING) {
      stateName = "sleeping";
    } else if (thread->state == Thread::ThreadState::RUNNABLE) {
      stateName = "runnable";
    } else if (thread->state == Thread::ThreadState::EXITED) {
      stateName = "exited";
    } else {
      stateName = "unknown";
    }

    out << "Tid: " << thread->tid << " in state: " << stateName << "\n";
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
