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
  namespace event {
    class event;
  }
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

  static const ThreadId mainThreadId;

private:
  ExecutionState() = delete;
  ExecutionState &operator=(const ExecutionState &) = delete;

  /// @brief The sync point where we wait for the threads
  uint64_t currentSchedulingIndex;

  /// @brief tracks and checks all memory accesses
  DataRaceDetection raceDetection;

public:
  // Execution - Control Flow specific

  /// @brief Thread map representing all threads that exist at the moment
  std::map<ThreadId, Thread> threads;

  /// @brief currently selected thread
  Thread *current = nullptr;

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
  std::vector<std::pair<ref<const MemoryObject>, const Array *>> symbolics;

  /// @brief Set of used array names for this state.  Used to avoid collisions.
  std::set<std::string> arrayNames;

  MemoryState memoryState;

  // node for Partial Order Reduction based exploration
  por::node* porNode = nullptr;

  // FIXME: solve in a better way
  por::node* lastPorNode = nullptr;

  // sequence of events that need to be caught up
  std::deque<por::event::event const *> catchUp;

  // The numbers of times this state has run through Executor::stepInstruction
  std::uint64_t steppedInstructions;

  std::uint64_t threadsCreated;

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
  Thread &thread() const { return *current; }

  /// @brief returns the ID of the current thread (only valid for one 'klee instruction')
  const ThreadId &tid() const { return current->tid; }

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

  std::optional<std::reference_wrapper<const Thread>> getThreadByRuntimeStructPtr(ref<Expr> expr) const {
    // For now we assume that the runtime struct ptr is unique for every pthread object in the runtime.
    // (At the current time, this is guaranteed with the pthread implementation)
    for (const auto& [tid, thread] : threads) {
      if (thread.runtimeStructPtr == expr) {
        return thread;
      }
    }
    return {};
  }

  bool isOnMainThread() const {
    return tid() == mainThreadId;
  }

  const auto& getDataRaceStats() const {
    return raceDetection.getStats();
  }

  KInstIterator pc(const Thread &thread) const { return thread.pc; }
  KInstIterator pc() const { return pc(thread()); }

  KInstIterator prevPc(const Thread &thread) const { return thread.prevPc; }
  KInstIterator prevPc() const { return prevPc(thread()); }

  StackFrame &stackFrame(Thread &thread) {
    assert(!thread.stack.empty());
    return thread.stack.back();
  }
  StackFrame &stackFrame() { return stackFrame(thread()); }
  const StackFrame &stackFrame(const Thread &thread) const {
    assert(!thread.stack.empty());
    return thread.stack.back();
  }
  const StackFrame &stackFrame() const { return stackFrame(thread()); }

  std::size_t stackFrameIndex(Thread &thread) const {
    assert(!thread.stack.empty());
    return thread.stack.size() - 1;
  }
  std::size_t stackFrameIndex() const { return stackFrameIndex(thread()); }

  Thread::stack_ty &stack(Thread &thread) {
    return thread.stack;
  }
  Thread::stack_ty &stack() { return stack(thread()); }
  const Thread::stack_ty &stack(const Thread &thread) const {
    return thread.stack;
  }
  const Thread::stack_ty &stack() const { return stack(thread()); }

  MemoryFingerprint &threadFingerprint(Thread &thread) {
    return thread.fingerprint;
  }
  MemoryFingerprint &threadFingerprint() { return threadFingerprint(thread()); }

  const MemoryObject *errnoMo() { return thread().errnoMo; }

  /// @brief will create a new thread
  Thread &createThread(KFunction *kf, ref <Expr> runtimeStructPtr);

  ThreadState threadState(const Thread &thread) const { return thread.state; }
  ThreadState threadState() const { return threadState(thread()); }

  /// @brief will exit the current thread
  void exitThread(bool callToExit);

  /// @brief will mark the referenced thread as cutoff
  void cutoffThread(Thread &thread);
  void cutoffThread() { cutoffThread(thread()); }

  Thread::waiting_t runThread(Thread &thread);

  void blockThread(Thread &thread, Thread::waiting_t blockOn);
  void blockThread(Thread::waiting_t blockOn) { blockThread(thread(), std::move(blockOn)); }

  std::set<ThreadId> runnableThreads();

  void pushFrame(KInstIterator caller, KFunction *kf) {
    thread().pushFrame(caller, kf);
  }

  void popFrameOfThread() { popFrameOfThread(thread()); }

  void addSymbolic(const MemoryObject *mo, const Array *array);
  void addConstraint(ref<Expr> e) { constraints.addConstraint(e); }

  bool hasUnregisteredDecisions() const noexcept {
    return !thread().pathSincePorLocal.empty();
  }

  auto &unregisteredDecisions() const noexcept {
    return thread().pathSincePorLocal;
  }

  void addDecision(Thread::decision_t decision) noexcept {
    assert(std::none_of(threads.begin(), threads.end(), [this](auto& it) {
      const Thread &thread = it.second;
      if (thread.tid == current->tid) {
        return false;
      } else {
        return !thread.pathSincePorLocal.empty();
      }
    }));
    thread().pathSincePorLocal.emplace_back(decision);
  }

  void addDecision(const Array *array) noexcept {
    addDecision(Thread::decision_array_t{array});
  }

  void addDecision(std::uint64_t branch, ref<Expr> expr) noexcept {
    addDecision(Thread::decision_branch_t{branch, std::move(expr)});
  }

  void addDecision(ref<Expr> expr) noexcept {
    addDecision(Thread::decision_constraint_t{std::move(expr)});
  }

  auto peekDecision() const noexcept {
    assert(peekCatchUp() && peekCatchUp()->kind() == por::event::event_kind::local);
    auto decision = thread().getNextDecisionFromLocal(*peekCatchUp());
    return decision;
  }

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

  void performAllocatorFree(const MemoryObject* mo);
  void performAllocatorFree(const por::event::event &event);

  void dumpStack(llvm::raw_ostream &out) const;
  void dumpSchedulingInfo(llvm::raw_ostream &out) const;
  void dumpAllThreadStacks(llvm::raw_ostream &out) const;
};

std::size_t klee_state_id(const ExecutionState *state);

}

#endif /* KLEE_EXECUTIONSTATE_H */
