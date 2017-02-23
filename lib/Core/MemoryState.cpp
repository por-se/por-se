#include "AddressSpace.h"
#include "DebugInfiniteLoopDetection.h"
#include "Memory.h"
#include "MemoryState.h"

#include "klee/Internal/Module/InstructionInfoTable.h"

#include "llvm/Support/raw_ostream.h"

#include <cmath>
#include <iomanip>
#include <sstream>

namespace klee {

void MemoryState::registerExternalFunctionCall() {
  if (optionIsSet(DebugInfiniteLoopDetection, STDERR_STATE)) {
    llvm::errs() << "MemoryState: external function call\n";
  }

  // it is unknown whether control flow is changed by an external function
  trace.clear();

  // make all previous changes to fingerprint permanent
  fingerprint.resetDelta();
}

void MemoryState::registerAllocation(const MemoryObject &mo) {
  fingerprint.updateUint8(1);
  fingerprint.updateUint64(mo.address);
  fingerprint.updateUint64(mo.size);

  fingerprint.applyToFingerprint();

  if (optionIsSet(DebugInfiniteLoopDetection, STDERR_STATE)) {
    llvm::errs() << "MemoryState: processing (de)allocation at address "
                 << mo.address << " of size " << mo.size
                 << " [fingerprint: " << fingerprint.getFingerprintAsString()
                 << "]\n";
  }
}

void MemoryState::registerWrite(ref<Expr> base, const MemoryObject &mo,
                                const ObjectState &os) {

  if (optionIsSet(DebugInfiniteLoopDetection, STDERR_STATE)) {
    llvm::errs() << "MemoryState: processing ObjectState at base address "
                 << ExprString(base) << "\n";
  }

  if (!allocasInCurrentStackFrame)
    allocasInCurrentStackFrame = true;

  ConstantExpr *constantBase = dyn_cast<ConstantExpr>(base);

  for (std::uint64_t offset = 0; offset < os.size; offset++) {
    // add base address to fingerprint
    if (constantBase) {
      // concrete address
      fingerprint.updateUint8(2);
      assert(constantBase->getWidth() <= 64 && "address greater than 64 bit!");
      std::uint64_t address = constantBase->getZExtValue(64);
      fingerprint.updateUint64(address);
    } else {
      // symbolic address
      fingerprint.updateUint8(3);
      fingerprint.updateExpr(base);
    }

    // add current offset to fingerprint
    fingerprint.updateUint64(offset);

    if (optionIsSet(DebugInfiniteLoopDetection, STDERR_STATE)) {
      llvm::errs() << "[+" << offset << "] ";
    }

    // add value of byte at offset to fingerprint
    ref<Expr> valExpr = os.read8(offset);
    if (ConstantExpr *constant = dyn_cast<ConstantExpr>(valExpr)) {
      // concrete value
      fingerprint.updateUint8(0);
      std::uint8_t value = constant->getZExtValue(8);
      fingerprint.updateUint8(value);
      if (optionIsSet(DebugInfiniteLoopDetection, STDERR_STATE)) {
        llvm::errs() << "0x";
        llvm::errs().write_hex((int)value);
      }
    } else {
      // symbolic value
      fingerprint.updateUint8(1);
      fingerprint.updateExpr(valExpr);
      if (optionIsSet(DebugInfiniteLoopDetection, STDERR_STATE)) {
        llvm::errs() << ExprString(valExpr);
      }
    }

    fingerprint.applyToFingerprint();

    if (optionIsSet(DebugInfiniteLoopDetection, STDERR_STATE)) {
      llvm::errs() << " [fingerprint: "
                   << fingerprint.getFingerprintAsString() << "]\n";
    }
  }
}

void MemoryState::registerLocal(const KInstruction *target, ref<Expr> value) {
  fingerprint.updateUint8(4);
  fingerprint.updateUint64(reinterpret_cast<std::intptr_t>(target));

  if (ConstantExpr *constant = dyn_cast<ConstantExpr>(value)) {
    // concrete value
    fingerprint.updateConstantExpr(*constant);
  } else {
    // symbolic value
    fingerprint.updateExpr(value);
  }

  fingerprint.applyToFingerprintAndDelta();

  if (optionIsSet(DebugInfiniteLoopDetection, STDERR_STATE)) {
    const InstructionInfo &ii = *target->info;
    llvm::errs() << "MemoryState: adding local to instruction "
                 << reinterpret_cast<std::intptr_t>(target)
                 << " (" << ii.file << ":" << ii.line << ":" << ii.id << ")"
                 << ": " << ExprString(value) << "\n"
                 << " [fingerprint: " << fingerprint.getFingerprintAsString()
                 << "]\n";
  }
}

void MemoryState::registerArgument(const KFunction *kf, unsigned index,
                                   ref<Expr> value) {
  fingerprint.updateUint8(5);
  fingerprint.updateUint64(reinterpret_cast<std::intptr_t>(kf));
  fingerprint.updateUint64(index);

  if (ConstantExpr *constant = dyn_cast<ConstantExpr>(value)) {
    // concrete value
    fingerprint.updateConstantExpr(*constant);
  } else {
    // symbolic value
    fingerprint.updateExpr(value);
  }

  fingerprint.applyToFingerprintAndDelta();

  if (optionIsSet(DebugInfiniteLoopDetection, STDERR_STATE)) {
    llvm::errs() << "MemoryState: adding argument " << index << " to function "
                 << reinterpret_cast<std::intptr_t>(kf) << ": "
                 << ExprString(value) << "\n"
                 << " [fingerprint: " << fingerprint.getFingerprintAsString()
                 << "]\n";
  }
}

void MemoryState::registerBasicBlock(const KInstruction *inst) {
  if (optionIsSet(DebugInfiniteLoopDetection, STDERR_STATE)) {
    llvm::errs() << "MemoryState: BASICBLOCK\n";
  }

  trace.registerBasicBlock(inst, fingerprint.getFingerprint());
}

bool MemoryState::findLoop() {
  bool result = trace.findLoop();

  if (optionIsSet(DebugInfiniteLoopDetection, STDERR_TRACE)) {
    if (result) {
      trace.debugStack();
    }
  }

  return result;
}

void MemoryState::registerPushFrame() {
  if (optionIsSet(DebugInfiniteLoopDetection, STDERR_STATE)) {
    llvm::errs() << "MemoryState: PUSHFRAME\n";
  }

  trace.registerEndOfStackFrame(fingerprint.getDelta(), allocasInCurrentStackFrame);

  // make locals and arguments "invisible"
  fingerprint.removeDelta();

  // reset stack frame specific information
  allocasInCurrentStackFrame = false;
}

void MemoryState::registerPopFrame() {
  if (optionIsSet(DebugInfiniteLoopDetection, STDERR_STATE)) {
    llvm::errs() << "MemoryState: POPFRAME\n"
                 << "Fingerprint: " << fingerprint.getFingerprintAsString()
                 << "\n";
  }

  if (trace.getNumberOfStackFrames() > 0) {
    // remove delta (locals and arguments) of stack frame that is to be left
    fingerprint.removeDelta();

    // make locals and argument "visible" again by
    // applying delta of stack frame that is to be entered
    auto previousFrame = trace.popFrame();
    fingerprint.applyDelta(previousFrame.first);

    allocasInCurrentStackFrame = previousFrame.second;

    if (optionIsSet(DebugInfiniteLoopDetection, STDERR_STATE)) {
      llvm::errs() << "reapplying delta: " << fingerprint.getDeltaAsString()
                   << "\nAllocas: " << allocasInCurrentStackFrame
                   << "\nFingerprint: " << fingerprint.getFingerprintAsString()
                   << "\n";
    }
  } else {
    // no stackframe left to pop

    if (optionIsSet(DebugInfiniteLoopDetection, STDERR_STATE)) {
      llvm::errs() << "no stackframe left in trace\n";
    }
  }
}


std::string MemoryState::ExprString(ref<Expr> expr) {
  std::string result;
  llvm::raw_string_ostream ostream(result);
  expr->print(ostream);
  ostream.flush();
  return result;
}

}
