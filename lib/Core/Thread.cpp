#include "klee/Thread.h"

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
          varargs(s.varargs) {
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

  assert(threadStartRoutine && "A thread has to start somewhere");

  // First stack frame is basically always the start routine of the thread
  // for the main thread this should be probably the main function
  this->stack.push_back(StackFrame(0, threadStartRoutine));

  // Short circuit the program counters
  this->prevPc = threadStartRoutine->instructions;
  this->pc = this->prevPc;
}

Thread::ThreadId Thread::getThreadId() {
  return this->tid;
}

void Thread::popStackFrame() {
  stack.pop_back();
}

void Thread::pushFrame(KInstIterator caller, KFunction *kf) {
  stack.push_back(StackFrame(caller, kf));
}