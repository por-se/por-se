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

Thread::MemoryAccess::MemoryAccess(uint8_t type, ref<Expr> offset, uint64_t epoch)
        : type(type), offset(offset), epoch(epoch), safeMemoryAccess(false) {}

Thread::MemoryAccess::MemoryAccess(const klee::Thread::MemoryAccess &a) = default;

Thread::Thread(ThreadId tid, KFunction* threadStartRoutine) {
  this->tid = tid;
  this->epochRunCount = 0;

  assert(threadStartRoutine && "A thread has to start somewhere");

  // First stack frame is basically always the start routine of the thread
  // for the main thread this should be probably the main function
  this->stack.push_back(StackFrame(nullptr, threadStartRoutine));

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
          state(t.state),
          syncPhaseAccesses(t.syncPhaseAccesses),
          threadSyncs(t.threadSyncs),
          epochRunCount(t.epochRunCount) {

  for (auto& access : syncPhaseAccesses) {
    access.first->refCount++;
  }
}

Thread::ThreadId Thread::getThreadId() const {
  return this->tid;
}

void Thread::popStackFrame() {
  stack.pop_back();
}

void Thread::pushFrame(KInstIterator caller, KFunction *kf) {
  stack.push_back(StackFrame(caller, kf));
}

bool Thread::trackMemoryAccess(const MemoryObject* target, MemoryAccess access) {
  auto it = syncPhaseAccesses.find(target);
  bool trackedNewObject = false;

  if (it == syncPhaseAccesses.end()) {
    auto insert = syncPhaseAccesses.insert(std::make_pair(target, std::vector<MemoryAccess>()));
    assert(insert.second && "Failed to insert element");
    it = insert.first;
    trackedNewObject = true;
  }

  bool newIsWrite = access.type & READ_ACCESS;
  bool newIsFree = access.type & FREE_ACCESS;
  bool newIsAlloc = access.type & ALLOC_ACCESS;

  // So there is already an entry. So go ahead and deduplicate as much as possible
  for (auto& accessIt : it->second) {
    // We can merge or extend in certain cases
    // Of course all merges can only be done if the accesses are in the same sync phase
    if (accessIt.epoch != access.epoch) {
      continue;
    }

    // Another important difference we should always consider is when two different accesses
    // conflict with their scheduling configuration
    if (accessIt.safeMemoryAccess != access.safeMemoryAccess) {
      continue;
    }

    // Every free or alloc call is stronger as any other access type and does not require
    // offset checks, so this is one of the simpler merges
    if (newIsAlloc || newIsFree) {
      accessIt.type = access.type;
      // alloc and free do not track the offset
      accessIt.offset = nullptr;
      return trackedNewObject;
    }

    // One special case where we can merge two entries: the previous one is a read
    // and now a write is done to the same offset (write is stronger)
    // Needs the same offsets to be correct
    if (newIsWrite && (accessIt.type & Thread::READ_ACCESS) && access.offset == accessIt.offset){
      accessIt.type = Thread::WRITE_ACCESS;
      return trackedNewObject;
    }
  }

  MemoryAccess newAccess (access);
  // Make sure that we always use the same epoch number
  newAccess.epoch = access.epoch;
  it->second.emplace_back(newAccess);
  return trackedNewObject;
}