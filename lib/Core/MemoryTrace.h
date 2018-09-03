#ifndef KLEE_MEMORYTRACE_H
#define KLEE_MEMORYTRACE_H

#include "MemoryFingerprint.h"

#include "klee/Internal/Module/KInstruction.h"

#include <vector>

namespace klee {
class ExecutionState;
struct KFunction;
class MemoryObject;
struct StackFrame;

class MemoryTrace {

using fingerprint_t = MemoryFingerprint::fingerprint_t;

public:
  struct StackFrameEntry {
    // function that is executed in this stack frame
    const KFunction *kf;
    // locals and arguments only visible within this stack frame
    fingerprint_t fingerprintLocalDelta;
    // allocas allocated in this stack frame
    fingerprint_t fingerprintAllocaDelta;

    StackFrameEntry(const KFunction *kf,
                    fingerprint_t fingerprintLocalDelta,
                    fingerprint_t fingerprintAllocaDelta)
        : kf(kf),
          fingerprintLocalDelta(fingerprintLocalDelta),
          fingerprintAllocaDelta(fingerprintAllocaDelta) {}
  };

private:
  std::vector<StackFrameEntry> stackFrames;

public:
  MemoryTrace() = default;
  MemoryTrace(const MemoryTrace &) = default;

  size_t getStackLength() const {
    return stackFrames.size();
  }

  size_t getStackCapacity() const {
    return stackFrames.capacity();
  }

  static size_t getStackStructSize() {
    return sizeof(StackFrameEntry);
  }

  void registerEndOfStackFrame(const KFunction *kf,
                               fingerprint_t fingerprintLocalDelta,
                               fingerprint_t fingerprintAllocaDelta);
  StackFrameEntry popFrame();
  void clear();
  std::size_t getNumberOfStackFrames();

  static bool isAllocaAllocationInCurrentStackFrame(const ExecutionState &state,
                                                    const MemoryObject &mo);
  fingerprint_t *getPreviousAllocaDelta(const ExecutionState &state,
                                        const MemoryObject &mo);
};
}

#endif
