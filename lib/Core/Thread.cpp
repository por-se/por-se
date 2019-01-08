#include "klee/Thread.h"
#include "Memory.h"

#include "CallPathManager.h"

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

Thread::Thread(ThreadId tid, KFunction* threadStartRoutine) {
  this->tid = tid;
  this->threadSchedulingWasDisabled = false;

  assert(threadStartRoutine && "A thread has to start somewhere");

  // First stack frame is basically always the start routine of the thread
  // for the main thread this should be probably the main function
  this->stack.push_back(StackFrame(nullptr, threadStartRoutine));

  // Short circuit the program counters
  this->prevPc = threadStartRoutine->instructions;
  this->pc = this->prevPc;
}

Thread::Thread(const Thread &t)
        : pc(t.pc),
          prevPc(t.prevPc),
          stack(t.stack),
          tid(t.tid),
          incomingBBIndex(t.incomingBBIndex),
          state(t.state),
          startArg(t.startArg),
          errnoMo(t.errnoMo),
          threadSchedulingWasDisabled(t.threadSchedulingWasDisabled) {
}

Thread::ThreadId Thread::getThreadId() const {
  return tid;
}

ref<Expr> Thread::getStartArgument() const {
  return startArg;
}

void Thread::popStackFrame() {
  stack.pop_back();
}

void Thread::pushFrame(KInstIterator caller, KFunction *kf) {
  stack.push_back(StackFrame(caller, kf));
}
