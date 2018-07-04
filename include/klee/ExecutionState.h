//===-- ExecutionState.h ----------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_EXECUTIONSTATE_H
#define KLEE_EXECUTIONSTATE_H

#include "klee/Constraints.h"
#include "klee/Expr.h"
#include "klee/Internal/ADT/TreeStream.h"
#include "klee/MergeHandler.h"
#include "klee/Thread.h"

// FIXME: We do not want to be exposing these? :(
#include "../../lib/Core/AddressSpace.h"
#include "klee/Internal/Module/KInstIterator.h"

#include <map>
#include <set>
#include <vector>

namespace klee {
class Array;
class CallPathNode;
struct Cell;
struct KFunction;
struct KInstruction;
class MemoryObject;
class PTreeNode;
struct InstructionInfo;

llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const MemoryMap &mm);

/// @brief ExecutionState representing a path under exploration
class ExecutionState {
public:
  typedef std::vector<StackFrame> stack_ty;
  typedef std::map<Thread::ThreadId, Thread> threads_ty;

private:
  // unsupported, use copy constructor
  ExecutionState &operator=(const ExecutionState &);

  std::map<std::string, std::string> fnAliases;

  /// @brief Pointer to the thread that is currently executed
  threads_ty::iterator currentThreadIterator;

  /// @brief The sync point where we wait for the threads
  uint64_t currentSynchronizationPoint;

public:
  // Execution - Control Flow specific

  /// @brief Thread map representing all threads that exist at the moment
  threads_ty threads;

  /// @brief the history of scheduling up until now
  std::vector<Thread::ThreadId> schedulingHistory;

  /// @brief if thread scheduling is enabled at the current time
  bool threadSchedulingEnabled;

  /// @brief count of all threads that are not sleeping and are not dead
  unsigned liveThreadCount;

  // Overall state of the state - Data specific

  /// @brief Address space used by this state (e.g. Global and Heap)
  AddressSpace addressSpace;

  /// @brief Constraints collected so far
  ConstraintManager constraints;

  /// Statistics and information

  /// @brief Costs for all queries issued for this state, in seconds
  mutable double queryCost;

  /// @brief Weight assigned for importance of this state.  Can be
  /// used for searchers to decide what paths to explore
  double weight;

  /// @brief Exploration depth, i.e., number of times KLEE branched for this state
  unsigned depth;

  /// @brief History of complete path: represents branches taken to
  /// reach/create this state (both concrete and symbolic)
  TreeOStream pathOS;

  /// @brief History of symbolic path: represents symbolic branches
  /// taken to reach/create this state
  TreeOStream symPathOS;

  /// @brief Counts how many instructions were executed since the last new
  /// instruction was covered.
  unsigned instsSinceCovNew;

  /// @brief Whether a new instruction was covered in this state
  bool coveredNew;

  /// @brief Disables forking for this state. Set by user code
  bool forkDisabled;

  /// @brief Set containing which lines in which files are covered by this state
  std::map<const std::string *, std::set<unsigned> > coveredLines;

  /// @brief Pointer to the process tree of the current state
  PTreeNode *ptreeNode;

  /// @brief Ordered list of symbolics: used to generate test cases.
  //
  // FIXME: Move to a shared list structure (not critical).
  std::vector<std::pair<const MemoryObject *, const Array *> > symbolics;

  /// @brief Set of used array names for this state.  Used to avoid collisions.
  std::set<std::string> arrayNames;

  std::string getFnAlias(std::string fn);
  void addFnAlias(std::string old_fn, std::string new_fn);
  void removeFnAlias(std::string fn);

  // The objects handling the klee_open_merge calls this state ran through
  std::vector<ref<MergeHandler> > openMergeStack;

  // The numbers of times this state has run through Executor::stepInstruction
  std::uint64_t steppedInstructions;

private:
  ExecutionState() : ptreeNode(0) {}

  std::vector<const MemoryObject *> popFrameOfThread(Thread* thread);

  void dumpStackOfThread(llvm::raw_ostream &out, const Thread* thread) const;
public:
  ExecutionState(KFunction *kf);

  // XXX total hack, just used to make a state so solver can
  // use on structure
  ExecutionState(const std::vector<ref<Expr> > &assumptions);

  ExecutionState(const ExecutionState &state);

  ~ExecutionState();

  ExecutionState *branch();

  /// @brief returns the reference to the current thread (only valid for one 'klee instruction')
  Thread* getCurrentThreadReference() const;

  // The method below is a bit 'unstable' with regards to the thread id
  // -> probably at a later state the thread id will be created by the ExecutionState
  /// @brief will create a new thread with the given thread id
  Thread* createThread(Thread::ThreadId tid, KFunction *kf);

  //// @brief will put the current thread into sleep mode
  void sleepCurrentThread();

  /// @brief wakes a specific thread up
  void wakeUpThread(Thread::ThreadId tid);

  /// @brief wakes up a number of threads at once
  void wakeUpThreads(std::vector<Thread::ThreadId> tids);

  /// @brief will preempt the current thread for the current sync phase
  void preemptCurrentThread();

  /// @brief will exit the referenced thread
  void exitThread(Thread::ThreadId tid);

  /// @brief returns all runnable threads
  std::vector<Thread*> calculateRunnableThreads();

  /// @brief update the current scheduled thread
  void setCurrentScheduledThread(Thread::ThreadId tid);

  bool moveToNewSyncPhase();

  void trackMemoryAccess(const MemoryObject* mo, ref<Expr> offset, uint8_t type);

  std::vector<const MemoryObject *> popFrameOfCurrentThread();

  void addSymbolic(const MemoryObject *mo, const Array *array);
  void addConstraint(ref<Expr> e) { constraints.addConstraint(e); }

  bool merge(const ExecutionState &b);
  void dumpStack(llvm::raw_ostream &out) const;
  void dumpSchedulingInfo(llvm::raw_ostream &out) const;
  void dumpAllThreadStacks(llvm::raw_ostream &out) const;
};
}

#endif
