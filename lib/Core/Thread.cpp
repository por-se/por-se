#include "klee/Thread.h"
#include "Memory.h"

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

Thread::MemoryAccess::MemoryAccess(uint8_t type, ref<Expr> offset, uint64_t syncPhase)
        : type(type), offset(offset), syncPhase(syncPhase) {}

Thread::MemoryAccess::MemoryAccess(const klee::Thread::MemoryAccess &a)
        : type(a.type), offset(a.offset), syncPhase(a.syncPhase) {}

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

Thread::Thread(const klee::Thread &t)
        : pc(t.pc),
          prevPc(t.prevPc),
          stack(t.stack),
          tid(t.tid),
          incomingBBIndex(t.incomingBBIndex),
          synchronizationPoint(t.synchronizationPoint),
          state(t.state),
          syncPhaseAccesses(t.syncPhaseAccesses) {

  for (auto& access : syncPhaseAccesses) {
    access.first->refCount++;
  }
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

bool Thread::trackMemoryAccess(const MemoryObject* target, ref<Expr> offset, uint8_t type) {
  auto it = syncPhaseAccesses.find(target);
  bool trackedNewObject = false;

  if (it == syncPhaseAccesses.end()) {
    auto insert = syncPhaseAccesses.insert(std::make_pair(target, std::vector<MemoryAccess>()));
    assert(insert.second && "Failed to insert element");
    it = insert.first;
    trackedNewObject = true;
  }

  // So there is already an entry. So go ahead and deduplicate as much as possible
  for (auto& accessIt : it->second) {
    if (accessIt.type != type) {
      // We cannot extend a memory access that is different from the one we currently process
      continue;
    }

    if (accessIt.offset == offset && accessIt.syncPhase == synchronizationPoint) {
      // It is already tracked so just bail out
      return trackedNewObject;
    }
  }

  it->second.emplace_back(MemoryAccess(type, offset, synchronizationPoint));
  return trackedNewObject;
}