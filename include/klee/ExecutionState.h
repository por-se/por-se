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

#include "klee/Expr/Constraints.h"
#include "klee/Expr/Expr.h"
#include "klee/Internal/ADT/TreeStream.h"
#include "klee/Internal/System/Time.h"
#include "klee/Thread.h"

// FIXME: We do not want to be exposing these? :(
#include "../../lib/Core/AddressSpace.h"
#include "../../lib/Core/RaceDetection/DataRaceDetection.h"
#include "../../lib/Core/MemoryState.h"
#include "klee/Internal/Module/KInstIterator.h"

#include <functional>
#include <map>
#include <optional>
#include <set>
#include <vector>

namespace por {
  class leaf;
  class node;
};

namespace klee {
class Array;
class CallPathNode;
struct Cell;
struct KFunction;
struct KInstruction;
class MemoryObject;
class PTreeNode;
struct InstructionInfo;
class Executor;

llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const MemoryMap &mm);

/// @brief ExecutionState representing a path under exploration
class ExecutionState {
  friend class Executor;

protected:
  static size_t next_id;

public:
  const size_t id;

  typedef std::map<ThreadId, Thread> threads_ty;

  static const ThreadId mainThreadId;

private:
  ExecutionState() = delete;
  ExecutionState &operator=(const ExecutionState &) = delete;

  /// @brief Pointer to the thread that is currently executed
  Thread *_currentThread = nullptr;

  Thread &currentThread(Thread &thread) {
    _currentThread = &thread;
    return *_currentThread;
  }

  Thread &currentThread(const ThreadId &tid) {
    auto it = threads.find(tid);
    assert(it != threads.end() && "Invalid thread ID");
    _currentThread = &it->second;
    return *_currentThread;
  }

  /// @brief The sync point where we wait for the threads
  uint64_t currentSchedulingIndex;

  /// @brief tracks and checks all memory accesses
  DataRaceDetection raceDetection;

public:
  // Execution - Control Flow specific

  /// @brief Thread map representing all threads that exist at the moment
  threads_ty threads;

  /// @brief True if scheduleThreads() should be run after current instruction
  bool needsThreadScheduling = false;

  /// @brief True if some thread has called exit() or equivalent
  bool calledExit = false;

  /// @brief the history of scheduling up until now
  std::vector<ThreadId> schedulingHistory;

  // Overall state of the state - Data specific

  /// @brief Address space used by this state (e.g. Global and Heap)
  AddressSpace addressSpace;

  /// @brief Constraints collected so far
  ConstraintManager constraints;

  /// Statistics and information

  /// @brief Costs for all queries issued for this state, in seconds
  mutable time::Span queryCost;

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
  PTreeNode *ptreeNode = nullptr;

  /// @brief Ordered list of symbolics: used to generate test cases.
  //
  // FIXME: Move to a shared list structure (not critical).
  std::vector<std::pair<const MemoryObject *, const Array *> > symbolics;

  /// @brief Set of used array names for this state.  Used to avoid collisions.
  std::set<std::string> arrayNames;

  MemoryState memoryState;

  // node for Partial Order Reduction based exploration
  por::node* porNode = nullptr;

  // sequence of events that need to be caught up
  std::deque<por::event::event const *> catchUp;

  // The numbers of times this state has run through Executor::stepInstruction
  std::uint64_t steppedInstructions;

private:
  void popFrameOfThread(Thread &thread);

  void dumpStackOfThread(llvm::raw_ostream &out, const Thread* thread) const;

public:
  ExecutionState(KFunction *kf);

  // XXX total hack, just used to make a state so solver can
  // use on structure
  ExecutionState(const std::vector<ref<Expr> > &assumptions);

  ExecutionState(const ExecutionState &state);

  ExecutionState(const por::leaf &leaf);

  ~ExecutionState();

  ExecutionState *branch();

  /// @brief returns a reference to the current thread (only valid for one 'klee instruction')
  Thread &currentThread() const {
    return *_currentThread;
  }

  /// @brief returns the ID of the current thread (only valid for one 'klee instruction')
  const ThreadId &currentThreadId() const {
    return _currentThread->tid;
  }

  std::optional<std::reference_wrapper<Thread>> getThreadById(const ThreadId &tid) {
    auto it = threads.find(tid);
    if (it == threads.end())
      return {};

    return it->second;
  }

  std::optional<std::reference_wrapper<const Thread>> getThreadById(const ThreadId &tid) const {
    auto it = threads.find(tid);
    if (it == threads.end())
      return {};

    return it->second;
  }

  bool isOnMainThread() const {
    return currentThreadId() == mainThreadId;
  }

  const auto& getDataRaceStats() const {
    return raceDetection.getStats();
  }

  KInstIterator pc(const Thread &thread) const { return thread.pc; }
  KInstIterator pc() const { return pc(currentThread()); }

  KInstIterator prevPc(const Thread &thread) const { return thread.prevPc; }
  KInstIterator prevPc() const { return prevPc(currentThread()); }

  StackFrame &stackFrame(Thread &thread) {
    assert(!thread.stack.empty());
    return thread.stack.back();
  }
  StackFrame &stackFrame() { return stackFrame(currentThread()); }
  const StackFrame &stackFrame(const Thread &thread) const {
    assert(!thread.stack.empty());
    return thread.stack.back();
  }
  const StackFrame &stackFrame() const { return stackFrame(currentThread()); }

  std::size_t stackFrameIndex(Thread &thread) const {
    assert(!thread.stack.empty());
    return thread.stack.size() - 1;
  }
  std::size_t stackFrameIndex() const { return stackFrameIndex(currentThread()); }

  Thread::stack_ty &stack(Thread &thread) {
    return thread.stack;
  }
  Thread::stack_ty &stack() { return stack(currentThread()); }
  const Thread::stack_ty &stack(const Thread &thread) const {
    return thread.stack;
  }
  const Thread::stack_ty &stack() const { return stack(currentThread()); }

  MemoryFingerprint &threadFingerprint(Thread &thread) {
    return thread.fingerprint;
  }
  MemoryFingerprint &threadFingerprint() { return threadFingerprint(currentThread()); }

  /// @brief will create a new thread
  Thread &createThread(KFunction *kf, ref <Expr> runtimeStructPtr);

  ThreadState threadState(const Thread &thread) const { return thread.state; }
  ThreadState threadState() const { return threadState(currentThread()); }

  /// @brief will exit the referenced thread
  void exitThread(Thread &thread);
  void exitThread() { exitThread(currentThread()); }

  /// @brief will mark the referenced thread as cutoff
  void cutoffThread(Thread &thread);
  void cutoffThread() { cutoffThread(currentThread()); }

  std::set<ThreadId> runnableThreads() const;

  void pushFrame(KInstIterator caller, KFunction *kf) {
    currentThread().pushFrame(caller, kf);
  }

  void popFrameOfThread() { popFrameOfThread(currentThread()); }

  void addSymbolic(const MemoryObject *mo, const Array *array);
  void addConstraint(ref<Expr> e) { constraints.addConstraint(e); }

  bool needsCatchUp() const noexcept {
    if (!porNode) return false;
    return !catchUp.empty();
  }

  por::event::event const *peekCatchUp() const noexcept {
    if(!needsCatchUp()) {
      return nullptr;
    }
    return catchUp.front();
  }

  void dumpStack(llvm::raw_ostream &out) const;
  void dumpSchedulingInfo(llvm::raw_ostream &out) const;
  void dumpAllThreadStacks(llvm::raw_ostream &out) const;
};
}

#endif /* KLEE_EXECUTIONSTATE_H */
