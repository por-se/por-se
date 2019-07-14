#include "klee/Thread.h"
#include "Memory.h"

#include "CallPathManager.h"
#include "pseudoalloc.h"

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
  os << "tid<";
  for (std::size_t i = 0; i < tid.size(); i++) {
    if (i > 0) {
      os << ",";
    }

    const std::uint16_t val = tid[i];
    os << val;
  }
  os << ">";
  return os;
}

/***/

Thread::Thread(ThreadId tid, KFunction* threadStartRoutine) : tid(std::move(tid)) {
  assert(threadStartRoutine && "A thread has to start somewhere");

  // in case of main thread, this is the program's entry point, e.g. main()
  this->stack.emplace_back(nullptr, threadStartRoutine);

  // initialize program counters
  this->prevPc = threadStartRoutine->instructions;
  this->pc = this->prevPc;

  this->runtimeStructPtr = ConstantExpr::createPointer(0);
}

Thread::Thread(const Thread &t)
        : pc(t.pc),
          prevPc(t.prevPc),
          liveSetPc(t.liveSetPc),
          stack(t.stack),
          tid(t.tid),
          incomingBBIndex(t.incomingBBIndex),
          state(t.state),
          waitingHandle(t.waitingHandle),
          runtimeStructPtr(t.runtimeStructPtr),
          errnoMo(t.errnoMo),
          threadSchedulingWasDisabled(t.threadSchedulingWasDisabled),
          pathSincePorLocal(t.pathSincePorLocal),
          spawnedThreads(t.spawnedThreads) {

  threadHeapAlloc = pseudoalloc::pseudoalloc_clone(t.threadHeapAlloc);
  threadStackAlloc = pseudoalloc::pseudoalloc_clone(t.threadStackAlloc);
}

Thread::~Thread() {
  if (threadHeapAlloc) {
    pseudoalloc::pseudoalloc_drop(threadHeapAlloc);
  }

  if (threadStackAlloc) {
    pseudoalloc::pseudoalloc_drop(threadStackAlloc);
  }
}


ThreadId Thread::getThreadId() const {
  return tid;
}

ref<Expr> Thread::getRuntimeStructPtr() const {
  return runtimeStructPtr;
}

void Thread::popStackFrame() {
  stack.pop_back();
}

void Thread::pushFrame(KInstIterator caller, KFunction *kf) {
  stack.emplace_back(caller, kf);
}
