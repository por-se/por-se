#include "MemoryTrace.h"

#include "InfiniteLoopDetectionFlags.h"
#include "MemoryFingerprint.h"

#include "klee/ExecutionState.h"
#include "klee/Internal/Module/InstructionInfoTable.h"
#include "klee/Internal/Support/ErrorHandling.h"

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/raw_ostream.h"

#include <iomanip>
#include <iterator>
#include <sstream>

namespace klee {

void MemoryTrace::registerEndOfStackFrame(const KInstruction* caller,
                                          fingerprint_t fingerprintLocalDelta,
                                          fingerprint_t fingerprintAllocaDelta)
{
  stackFrames.emplace_back(caller,
                           fingerprintLocalDelta,
                           fingerprintAllocaDelta);
}

std::size_t MemoryTrace::getNumberOfStackFrames() {
  return stackFrames.size();
}

MemoryTrace::StackFrameEntry MemoryTrace::popFrame() {
  assert(!stackFrames.empty());

  MemoryTrace::StackFrameEntry sfe = stackFrames.back();

  // remove topmost stack frame
  stackFrames.pop_back();

  if (DebugInfiniteLoopDetection.isSet(STDERR_TRACE)) {
    llvm::errs() << "Popping StackFrame\n";
  }

  return sfe;
}

bool MemoryTrace::isAllocaAllocationInCurrentStackFrame(
  const ExecutionState &state, const MemoryObject &mo)
{
  Thread &thread = state.getCurrentThreadReference();
  return (thread.stack.size() - 1 == mo.getStackframeIndex());
}

MemoryTrace::fingerprint_t *MemoryTrace::getPreviousAllocaDelta(
  const ExecutionState &state, const MemoryObject &mo) {
  assert(!isAllocaAllocationInCurrentStackFrame(state, mo));

  Thread &thread = state.getCurrentThreadReference();

  size_t index = mo.getStackframeIndex();

  // Compared to stackFrames, thread.stack contains at least one more stack
  // frame, i.e. the currently executed one (top most entry)
  assert(stackFrames.size() + 1 <= thread.stack.size());

  // smallest index that is present in MemoryTrace
  size_t smallestIndex = thread.stack.size() - (stackFrames.size() + 1);
  if (index < smallestIndex) {
    // MemoryTrace has been cleared since the time of allocation
    return nullptr;
  }

  StackFrameEntry &sfe = stackFrames.at(index - smallestIndex);
  return &sfe.fingerprintAllocaDelta;
}

}
