#include "MemoryState.h"

#include "AddressSpace.h"
#include "Memory.h"

#include "klee/ExecutionState.h"
#include "klee/Expr/Expr.h"
#include "klee/StatePruningCmdLine.h"
#include "klee/Internal/Module/Cell.h"
#include "klee/Internal/Module/InstructionInfoTable.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/KModule.h"
#include "klee/Internal/Support/ErrorHandling.h"

#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"

#include <cmath>
#include <iomanip>
#include <sstream>

namespace klee {

KModule *MemoryState::kmodule = nullptr;
std::vector<llvm::Function *> MemoryState::outputFunctionsWhitelist;
std::vector<llvm::Function *> MemoryState::libraryFunctionsList;
std::vector<llvm::Function *> MemoryState::memoryFunctionsList;

void MemoryState::setKModule(KModule *_kmodule) {
  if (kmodule != nullptr) return;

  // whitelist: output functions
  const char* outputFunctions[] = {
    // stdio.h
    "fflush", "fputc", "putc", "fputwc", "putwc", "fputs", "fputws", "putchar",
    "putwchar", "puts", "printf", "fprintf", "sprintf", "snprintf", "wprintf",
    "fwprintf", "swprintf", "vprintf", "vfprintf", "vsprintf", "vsnprintf",
    "vwprintf", "vfwprintf", "vswprintf", "perror",

    // POSIX
    "write"
  };

  // library function that might use heavy loops that we do not want to inspect
  const char* libraryFunctions[] = {
    // string.h
    "memcmp", "memchr", "strcpy", "strncpy", "strcat", "strncat", "strxfrm",
    "strlen", "strcmp", "strncmp", "strcoll", "strchr", "strrchr", "strspn",
    "strcspn", "strpbrk", "strstr",

    // wchar.h
    "wmemcmp", "wmemchr", "wcscpy", "wcsncpy", "wcscat", "wcsncat", "wcsxfrm",
    "wcslen", "wcscmp", "wcsncmp", "wcscoll", "wcschr", "wcsrchr", "wcsspn",
    "wcscspn", "wcspbrk", "wcsstr",

    // GNU
    "mempcpy",

    // POSIX
    "strdup", "strcasecmp", "memccpy", "bzero"
  };

  // library functions with signature (*dest, _, count) that modify the memory
  // starting from dest for count bytes
  const char* memoryFunctions[] = {
    "memset", "memcpy", "memmove", "wmemset", "wmemcpy", "wmemmove"
  };

  initializeFunctionList(_kmodule, outputFunctions, outputFunctionsWhitelist);
  initializeFunctionList(_kmodule, libraryFunctions, libraryFunctionsList);
  initializeFunctionList(_kmodule, memoryFunctions, memoryFunctionsList);

  kmodule = _kmodule;
}

template <std::size_t array_size>
void MemoryState::initializeFunctionList(KModule *_kmodule,
                                         const char* (& functions)[array_size],
                                         std::vector<llvm::Function *> &list) {
  std::vector<llvm::Function *> tmp;
  for (const char *name : functions) {
    llvm::Function *f = _kmodule->module->getFunction(name);
    if (f == nullptr) {
      if (DebugStatePruning) {
        llvm::errs() << "MemoryState: could not find function in module: "
                     << name << "\n";
      }
    } else {
      if (DebugStatePruning) {
        llvm::errs() << "MemoryState: found function in module: "
                     << name << "\n";
      }
      tmp.emplace_back(f);
    }
  }
  std::sort(tmp.begin(), tmp.end());
  list = std::move(tmp);
}


void MemoryState::leaveListedFunction() {
  if (DebugStatePruning) {
    llvm::errs() << "MemoryState: leaving listed function: "
                 << listedFunction.function->getName() << "\n";
  }

  listedFunction.entered = false;
  listedFunction.function = nullptr;

  updateDisableMemoryState();
}

bool MemoryState::enterLibraryFunction(llvm::Function *f) {
  if (libraryFunction.entered) {
    return false;
  }

  if (DebugStatePruning) {
    llvm::errs() << "MemoryState: entering library function: "
                 << f->getName() << "\n";
  }

  libraryFunction.entered = true;
  libraryFunction.function = f;

  updateDisableMemoryState();

  return true;
}

void MemoryState::leaveLibraryFunction() {
  if (DebugStatePruning) {
    llvm::errs() << "MemoryState: leaving library function: "
                 << libraryFunction.function->getName() << "\n";
  }

  libraryFunction.entered = false;
  libraryFunction.function = nullptr;

  updateDisableMemoryState();
}

bool MemoryState::enterMemoryFunction(llvm::Function *f,
  ref<ConstantExpr> address, const MemoryObject *mo, const ObjectState *os,
  std::size_t bytes) {
  if (memoryFunction.entered) {
    // we can only enter one library function at a time
    klee_warning_once(f, "already entered a memory function");
    return false;
  }

  if (DebugStatePruning) {
    llvm::errs() << "MemoryState: entering memory function: "
                 << f->getName() << "\n";
  }

  unregisterWrite(address, *mo, *os, bytes);

  memoryFunction.entered = true;
  memoryFunction.function = f;
  memoryFunction.address = address;
  memoryFunction.mo = mo;
  memoryFunction.bytes = bytes;

  updateDisableMemoryState();

  return true;
}

void MemoryState::leaveMemoryFunction() {
  if (DebugStatePruning) {
    llvm::errs() << "MemoryState: leaving memory function: "
                 << memoryFunction.function->getName() << "\n";
  }

  const MemoryObject *mo = memoryFunction.mo;
  const ObjectState *os = executionState->addressSpace.findObject(mo);

  memoryFunction.entered = false;
  memoryFunction.function = nullptr;

  updateDisableMemoryState();

  registerWrite(memoryFunction.address, *mo, *os, memoryFunction.bytes);
}

void MemoryState::registerFunctionCall(llvm::Function *f,
                                       std::vector<ref<Expr>> &arguments) {
  // NOTE: this method has to deal with the fact that a corresponding call to
  //       registerFunctionRet for f may not happen, as the call might be
  //       handled by SpecialFunctionHandler (or the function may not return).

  if (globalDisableMemoryState) {
    // we only check for global disable and not for library or listed functions
    // as we assume that those will not call any output functions
    return;
  }

  if (std::binary_search(outputFunctionsWhitelist.begin(),
                         outputFunctionsWhitelist.end(),
                         f)) {
    if (DebugStatePruning) {
      llvm::errs() << "MemoryState: whitelisted output function call to "
                   << f->getName() << "()\n";
    }
    enterListedFunction(f);
  } else if (std::binary_search(libraryFunctionsList.begin(),
                                libraryFunctionsList.end(),
                                f)) {
    if (DebugStatePruning) {
      llvm::errs() << "MemoryState: library function call to "
                   << f->getName() << "()\n";
    }
    enterLibraryFunction(f);
  } else if (std::binary_search(memoryFunctionsList.begin(),
                                memoryFunctionsList.end(),
                                f)) {
    ConstantExpr *constAddr = dyn_cast<ConstantExpr>(arguments[0]);
    ConstantExpr *constSize = dyn_cast<ConstantExpr>(arguments[2]);

    if (constAddr && constSize) {
      ObjectPair op;
      bool success;
      success = executionState->addressSpace.resolveOne(constAddr, op);

      if (success) {
        const MemoryObject *mo = op.first;
        const ObjectState *os = op.second;

        std::uint64_t count = constSize->getZExtValue(64);
        std::uint64_t addr = constAddr->getZExtValue(64);
        std::uint64_t offset = addr - mo->address;

        if (mo->size >= offset + count) {
          enterMemoryFunction(f, constAddr, mo, os, count);
        }
      }
    }
  }
}

void MemoryState::registerFunctionRet(llvm::Function *f) {
  if (isInListedFunction(f)) {
    leaveListedFunction();
  } else if (isInLibraryFunction(f)) {
    leaveLibraryFunction();
  } else if (isInMemoryFunction(f)) {
    leaveMemoryFunction();
  }
}

void MemoryState::registerWrite(ref<Expr> address, const MemoryObject &mo,
                                const ObjectState &os, std::size_t bytes) {
  if (disableMemoryState && !libraryFunction.entered) {
    // in case of library functions, we need to record changes to global memory
    // and previous allocas
    return;
  }

  if (DebugStatePruning) {
    ref<ConstantExpr> base = mo.getBaseExpr();
    llvm::errs() << "MemoryState: registering "
                 << (mo.isLocal ? "local " : "global ")
                 << "ObjectState at base address "
                 << ExprString(base) << "\n";
  }

  applyWriteFragment(address, mo, os, bytes, false);
}

void MemoryState::unregisterWrite(ref<Expr> address, const MemoryObject &mo,
                                  const ObjectState &os, std::size_t bytes) {
  if (disableMemoryState && !libraryFunction.entered) {
    // in case of library functions, we need to record changes to global memory
    // and previous allocas
    return;
  }

  if (DebugStatePruning) {
    ref<ConstantExpr> base = mo.getBaseExpr();
    llvm::errs() << "MemoryState: unregistering "
                 << (mo.isLocal ? "local " : "global ")
                 << "ObjectState at base address "
                 << ExprString(base) << "\n";
  }

  applyWriteFragment(address, mo, os, bytes, true);
}

void MemoryState::registerAcquiredLock(por::event::lock_id_t lock_id, const ThreadId &tid) {
  if (disableMemoryState) {
    return;
  }

  if (DebugStatePruning) {
    llvm::errs() << "MemoryState: registering acquired lock (" << lock_id <<  ")"
                 << " by thread " << tid << "\n";
  }

  auto& thread = executionState->currentThread();

  assert(tid && lock_id > 0 && "Must be set");
  assert(thread.getThreadId() == tid);

  auto &fingerprint = thread.fingerprint;
  fingerprint.updateAcquiredLockFragment(lock_id, tid);
  fingerprint.addToFingerprint();
}

void MemoryState::unregisterAcquiredLock(por::event::lock_id_t lock_id, const ThreadId &tid) {
  if (disableMemoryState) {
    return;
  }

  if (DebugStatePruning) {
    llvm::errs() << "MemoryState: unregistering acquired lock (" << lock_id <<  ")"
                 << " by thread " << tid << "\n";
  }

  auto& thread = executionState->currentThread();

  assert(tid && lock_id > 0 && "Must be set");
  assert(thread.getThreadId() == tid);

  auto &fingerprint = thread.fingerprint;
  fingerprint.updateAcquiredLockFragment(lock_id, tid);
  fingerprint.removeFromFingerprint();
}

void MemoryState::applyWriteFragment(ref<Expr> address, const MemoryObject &mo,
                                     const ObjectState &os, std::size_t bytes,
                                     bool remove) {
  if (libraryFunction.entered && mo.isLocal) {
    // change is only to be made to delta of current stack frame
    return;
  }

  auto &fingerprint = executionState->currentThread().fingerprint;

  ref<Expr> offset = mo.getOffsetExpr(address);
  ConstantExpr *concreteOffset = dyn_cast<ConstantExpr>(offset);

  std::uint64_t begin = 0;
  std::uint64_t end = os.size;

  MemoryFingerprintDelta *delta = nullptr;
  if (mo.isLocal) {
    std::pair<ThreadId, std::size_t> alloc = mo.getAllocationStackFrame();
    Thread &thread = executionState->threads.at(alloc.first);
    StackFrame &sf = thread.stack.at(alloc.second);
    delta = &sf.fingerprintDelta;
  }

  // optimization for concrete offsets: only hash changed indices
  if (concreteOffset) {
    begin = concreteOffset->getZExtValue(64);
    if ((begin + bytes) < os.size) {
      end = begin + bytes;
    }
  }

  std::uint64_t baseAddress = mo.getBaseExpr()->getZExtValue(64);
  bool endlineMissing = false;
  for (std::uint64_t i = begin; i < end; ++i) {
    // add value of byte at offset to fingerprint
    ref<Expr> valExpr = os.read8(i);
    std::uint64_t address = baseAddress + i;

    bool isSymbolic = fingerprint.updateWriteFragment(address, valExpr);
    if (!remove) {
      if (mo.isLocal) {
        fingerprint.addToFingerprintAndDelta(*delta);
      } else {
        fingerprint.addToFingerprint();
      }
    } else {
      if (mo.isLocal) {
        fingerprint.removeFromFingerprintAndDelta(*delta);
      } else {
        fingerprint.removeFromFingerprint();
      }
    }

    if (DebugStatePruning) {
      std::stringstream stream;
      stream << std::setw(std::floor(std::log10(end - 1)) + 1) << i;
      llvm::errs() << "[+" << stream.str() << "] ";
      if (isSymbolic) {
        llvm::errs() << ExprString(valExpr);
      } else {
        ConstantExpr *constant = cast<ConstantExpr>(valExpr);
        std::uint8_t value = constant->getZExtValue(8);
        llvm::errs() << "0x";
        if (value < 16) llvm::errs() << "0";
        llvm::errs().write_hex((int)value);
      }

      if (i % 10 == 9) {
        endlineMissing = false;
        llvm::errs() << "\n";
      } else {
        endlineMissing = true;
        llvm::errs() << " ";
      }
    }
  }
  if (DebugStatePruning && endlineMissing) {
    llvm::errs() << "\n";
  }
}

void MemoryState::registerArgument(const ThreadId &threadID,
                                   std::size_t sfIndex,
                                   const KFunction *kf,
                                   unsigned index,
                                   ref<Expr> value) {
  if (disableMemoryState) {
    return;
  }

  Thread &thread = executionState->currentThread();
  auto &fingerprint = thread.fingerprint;
  MemoryFingerprintDelta &delta = thread.stack.back().fingerprintDelta;
  fingerprint.updateArgumentFragment(threadID, sfIndex, kf, index, value);
  fingerprint.addToFingerprintAndDelta(delta);

  if (DebugStatePruning) {
    llvm::errs() << "MemoryState: adding argument " << index << " to function "
                 << reinterpret_cast<std::uintptr_t>(kf) << ": "
                 << ExprString(value) << "\n";
  }
}

bool MemoryState::enterListedFunction(llvm::Function *f) {
  if (listedFunction.entered) {
    // we can only enter one listed function at a time
    // (we do not need to register additional functions calls by the entered
    // function such as printf)
    return false;
  }

  if (DebugStatePruning) {
    llvm::errs() << "MemoryState: entering listed function: "
                 << f->getName() << "\n";
  }

  listedFunction.entered = true;
  listedFunction.function = f;

  updateDisableMemoryState();

  return true;
}

void MemoryState::registerPushFrame(const ThreadId &threadID,
                                    std::size_t sfIndex,
                                    const KFunction *callee,
                                    const KInstruction *caller) {
  // IMPORTANT: has to be called after state.pushFrame()
  if (DebugStatePruning) {
    llvm::errs() << "MemoryState: PUSHFRAME\n";
  }

  Thread &thread = executionState->currentThread();
  auto &fingerprint = thread.fingerprint;
  MemoryFingerprintDelta &delta = thread.stack.back().fingerprintDelta;

  // second but last stack frame
  std::size_t oldIndex = thread.stack.size() - 2;
  StackFrame &oldsf = thread.stack.at(oldIndex);

  // add locals and symbolic references to stackframe fingerprint
  // these will be automatically removed when the stack frame is popped
  for (const KInstruction *ki : caller->info->getLiveLocals()) {
    // NOTE: It is ok here to only consider locals that are live after the
    //       caller instruction, as the only locals that might be dead in
    //       comparison to the previous instruction are the ones passed as
    //       arguments to callee.
    ref<Expr> value = oldsf.locals[ki->dest].value;
    if (value.isNull())
      continue;

    auto tid = thread.getThreadId();
    fingerprint.updateLocalFragment(tid, oldIndex, ki->inst, value);
    fingerprint.addToFingerprintAndDelta(delta);
  }

  // register stack frame
  fingerprint.updateFunctionFragment(threadID, sfIndex, callee, caller);
  fingerprint.addToFingerprintAndDelta(delta);
}

void MemoryState::registerPopFrame(const StackFrame &sf) {
  if (DebugStatePruning) {
    llvm::errs() << "MemoryState: POPFRAME\n";
  }

  auto &fingerprint = executionState->currentThread().fingerprint;
  fingerprint.removeDelta(sf.fingerprintDelta);
}

MemoryFingerprintDelta MemoryState::getThreadDelta(const Thread &thread) const {
  MemoryFingerprint copy = thread.fingerprint;

  if (thread.state != ThreadState::Exited) {
    copy.updateProgramCounterFragment(thread.getThreadId(),
                                      thread.stack.size() - 1,
                                      thread.pc->inst);
    copy.addToFingerprint();

    // include live locals in current stack frame
    if (thread.liveSet != nullptr) {
      for (const KInstruction *ki : *thread.liveSet) {
        ref<Expr> value = thread.stack.back().locals[ki->dest].value;
        if (value.isNull())
          continue;

        copy.updateLocalFragment(thread.getThreadId(), thread.stack.size() - 1, ki->inst, value);
        copy.addToFingerprint();
      }
    }
  }

  return copy.getFingerprintAsDelta();
}

std::string MemoryState::ExprString(ref<Expr> expr) {
  std::string result;
  llvm::raw_string_ostream ostream(result);
  expr->print(ostream);
  ostream.flush();
  return result;
}

}
