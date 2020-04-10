#include "klee/Thread.h"

#include "CallPathManager.h"
#include "Memory.h"
#include "MemoryState.h"

#include "klee/Internal/Module/KInstruction.h"

#include "por/configuration.h"

#include "llvm/IR/Instructions.h"

#include <algorithm>
#include <sstream>
#include <vector>

using namespace llvm;
using namespace klee;

/***/

// Again the StackFrame sources are copied from the ExecutionState

StackFrame::StackFrame(KInstIterator _caller, KFunction *_kf)
        : caller(_caller), kf(_kf), callPathNode(0),
          minDistToUncoveredOnReturn(0), varargs(0) {
  locals = new Cell[kf->numRegisters];
}

StackFrame::StackFrame(const StackFrame &s)
        : caller(s.caller),
          kf(s.kf),
          callPathNode(s.callPathNode),
          allocas(s.allocas),
          minDistToUncoveredOnReturn(s.minDistToUncoveredOnReturn),
          varargs(s.varargs),
          fingerprintDelta(s.fingerprintDelta) {
  locals = new Cell[s.kf->numRegisters];
  for (unsigned i = 0; i < s.kf->numRegisters; i++)
    locals[i] = s.locals[i];
}

StackFrame::~StackFrame() {
  delete[] locals;
}

/***/

llvm::raw_ostream &klee::operator<<(llvm::raw_ostream &os, const ThreadId &tid) {
  for (std::size_t i = 0; i < tid.size(); i++) {
    if (i > 0) {
      os << ",";
    }

    const std::uint16_t val = tid[i];
    os << val;
  }
  return os;
}

/***/

Thread::Thread(ThreadId tid, KFunction *entry) : pc(entry->instructions), prevPc(pc), tid(std::move(tid)) {
  assert(entry && "A thread has to start somewhere");

  // in case of main thread, this is the program's entry point, e.g. main()
  this->stack.emplace_back(nullptr, entry);

  this->runtimeStructPtr = ConstantExpr::createPointer(0);
}

Thread::Thread(const Thread &t)
        : pc(t.pc),
          prevPc(t.prevPc),
          pcFingerprintStep(t.pcFingerprintStep),
          liveSet(t.liveSet),
          stack(t.stack),
          tid(t.tid),
          incomingBBIndex(t.incomingBBIndex),
          state(t.state),
          waiting(t.waiting),
          runtimeStructPtr(t.runtimeStructPtr),
          errnoMo(t.errnoMo),
          pathSincePorLocal(t.pathSincePorLocal),
          spawnedThreads(t.spawnedThreads),
          fingerprint(t.fingerprint),
          unsynchronizedFrees(t.unsynchronizedFrees) {

  threadHeapAlloc = std::make_unique<pseudoalloc::allocator_t>(*t.threadHeapAlloc);
  threadStackAlloc = std::make_unique<pseudoalloc::stack_allocator_t>(*t.threadStackAlloc);
}

ThreadId Thread::getThreadId() const {
  return tid;
}

bool Thread::isRunnable(const por::configuration &configuration) const noexcept {
  if (state == ThreadState::Waiting) {
    return std::visit([&configuration](auto&& w) -> bool {
      using T = std::decay_t<decltype(w)>;
      if constexpr (std::is_same_v<T, wait_none_t>) {
        return true;
      } else if constexpr (std::is_same_v<T, wait_lock_t> || std::is_same_v<T, wait_cv_2_t>) {
        return configuration.can_acquire_lock(w.lock);
      } else if constexpr (std::is_same_v<T, wait_join_t>) {
        return configuration.last_of_tid(w.thread)->kind() == por::event::event_kind::thread_exit;
      } else {
        return false;
      }
    }, waiting);
  } else {
    return state == ThreadState::Runnable;
  }
}

MemoryFingerprintDelta Thread::getFingerprintDelta() const {
  MemoryFingerprint copy = fingerprint;

  if (state != ThreadState::Exited) {
    copy.updateProgramCounterFragment(getThreadId(),
                                      stack.size() - 1,
                                      pc->inst,
                                      pcFingerprintStep);
    copy.addToFingerprint();

    copy.updateThreadStateFragment(getThreadId(), static_cast<std::uint8_t>(state));
    copy.addToFingerprint();

    const ThreadId &threadId = getThreadId();
    if (std::visit([&copy, &threadId](auto&& w) -> bool {
      using T = std::decay_t<decltype(w)>;
      if constexpr (std::is_same_v<T, Thread::wait_lock_t>) {
        copy.updateThreadWaitingOnLockFragment(threadId, w.lock);
      } else if constexpr (std::is_same_v<T, Thread::wait_cv_1_t>) {
        copy.updateThreadWaitingOnCV_1Fragment(threadId, w.cond, w.lock);
      } else if constexpr (std::is_same_v<T, Thread::wait_cv_2_t>) {
        copy.updateThreadWaitingOnCV_2Fragment(threadId, w.cond, w.lock);
      } else if constexpr (std::is_same_v<T, Thread::wait_join_t>) {
        copy.updateThreadWaitingOnJoinFragment(threadId, w.thread);
      } else {
        return false;
      }
      return true;
    }, waiting)) {
      copy.addToFingerprint();
    }

    // include live locals in current stack frame
    if (liveSet != nullptr) {
      for (const KInstruction *ki : *liveSet) {
        assert(ki->inst->getFunction() == stack.back().kf->function);
        ref<Expr> value = stack.back().locals[ki->dest].value;
        if (value.isNull())
          continue;

        copy.updateLocalFragment(getThreadId(), stack.size() - 1, ki->inst, value);
        copy.addToFingerprint();
      }
    }
  }

  return copy.getFingerprintAsDelta();
}

void Thread::popStackFrame() {
  stack.pop_back();
}

void Thread::pushFrame(KInstIterator caller, KFunction *kf) {
  stack.emplace_back(caller, kf);
}

template<>
std::string Thread::local_event_t::path_string() const noexcept {
  std::stringstream os;
  for (auto &d : path()) {
    assert(std::holds_alternative<Thread::decision_branch_t>(d));
    os << std::get<Thread::decision_branch_t>(d).branch;
  }
  return os.str();
}

void Thread::dumpLiveSet(llvm::raw_ostream &os) const noexcept {
  std::vector<const Value *> values;
  for (auto const *ki : *liveSet) {
    values.push_back(ki->inst);
  }
  std::sort(values.begin(), values.end(), [](const Value *a, const Value *b) {
    if (!a->hasName())
      return false;
    if (b->hasName())
      return a->getName() < b->getName();
    return true;
  });
  os << "liveSet: {";
  bool first = true;
  for (auto *inst : values) {
    if (!first) {
      os << " ";
    } else {
      first = false;
    }
    os << "%";
    if (inst->hasName()) {
      os << inst->getName();
    } else {
      // extract slot number
      std::string line;
      llvm::raw_string_ostream sos(line);
      sos << *inst;
      sos.flush();
      std::size_t start = line.find("%") + 1;
      std::size_t end = line.find(" ", start);
      os << line.substr(start, end - start);
    }
  }
  os << "}\n";
}
