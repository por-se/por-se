#include "MemoryState.h"

#include "AddressSpace.h"
#include "InfiniteLoopDetectionFlags.h"
#include "Memory.h"

#include "klee/ExecutionState.h"
#include "klee/Expr.h"
#include "klee/Internal/Module/Cell.h"
#include "klee/Internal/Module/InstructionInfoTable.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/KModule.h"
#include "klee/Internal/Support/ErrorHandling.h"
#include "klee/util/ExprVisitor.h"

#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"

#include <cmath>
#include <iomanip>
#include <sstream>

namespace klee {

size_t MemoryState::externalFunctionCallCounter = 0;
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
      if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
        llvm::errs() << "MemoryState: could not find function in module: "
                     << name << "\n";
      }
    } else {
      if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
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
  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
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

  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    llvm::errs() << "MemoryState: entering library function: "
                 << f->getName() << "\n";
  }

  libraryFunction.entered = true;
  libraryFunction.function = f;

  updateDisableMemoryState();

  return true;
}

void MemoryState::leaveLibraryFunction() {
  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
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

  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
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
  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
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
  if (globalDisableMemoryState) {
    // we only check for global disable and not for library or listed functions
    // as we assume that those will not call any output functions
    return;
  }

  // XXX: prototype same-state detection
  /*if (std::binary_search(outputFunctionsWhitelist.begin(),
                         outputFunctionsWhitelist.end(),
                         f)) {
    if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
      llvm::errs() << "MemoryState: whitelisted output function call to "
                   << f->getName() << "()\n";
    }
    enterListedFunction(f);
  } else*/ if (std::binary_search(libraryFunctionsList.begin(),
                                libraryFunctionsList.end(),
                                f)) {
    if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
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


void MemoryState::registerExternalFunctionCall() {
  if (listedFunction.entered) {
    return;
  }

  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    llvm::errs() << "MemoryState: external function call\n";
  }

  // it is unknown whether control flow is changed by an external function, so
  // we cannot make any assumptions about the state after this call

  // mask fingerprint with global counter
  fingerprint.updateUint8(9);
  fingerprint.updateUint64(externalFunctionCallCounter++);
  fingerprint.applyToFingerprint();
}

void MemoryState::registerWrite(ref<Expr> address, const MemoryObject &mo,
                                const ObjectState &os, std::size_t bytes) {
  if (disableMemoryState && !libraryFunction.entered) {
    // in case of library functions, we need to record changes to global memory
    // and previous allocas
    return;
  }

  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    ref<ConstantExpr> base = mo.getBaseExpr();
    llvm::errs() << "MemoryState: registering "
                 << (mo.isLocal ? "local " : "global ")
                 << "ObjectState at base address "
                 << ExprString(base) << "\n";
  }

  applyWriteFragment(address, mo, os, bytes, true);

  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    llvm::errs() << " [fingerprint: "
                 << fingerprint.getFingerprintAsString() << "]\n";
  }
}

void MemoryState::unregisterWrite(ref<Expr> address, const MemoryObject &mo,
                                  const ObjectState &os, std::size_t bytes) {
 if (disableMemoryState) {
    return;
  }

  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    ref<ConstantExpr> base = mo.getBaseExpr();
    llvm::errs() << "MemoryState: unregistering "
                 << (mo.isLocal ? "local " : "global ")
                 << "ObjectState at base address "
                 << ExprString(base) << "\n";
  }

  applyWriteFragment(address, mo, os, bytes, false);

  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    llvm::errs() << " [fingerprint: "
                 << fingerprint.getFingerprintAsString() << "]\n";
  }
}

void MemoryState::applyWriteFragment(ref<Expr> address, const MemoryObject &mo,
                                     const ObjectState &os, std::size_t bytes,
                                     bool increaseReferenceCount) {
  ref<Expr> offset = mo.getOffsetExpr(address);
  ConstantExpr *concreteOffset = dyn_cast<ConstantExpr>(offset);

  std::uint64_t begin = 0;
  std::uint64_t end = os.size;

  bool isLocal = false;
  MemoryFingerprint::fingerprint_t *externalDelta = nullptr;

  if (mo.isLocal) {
    isLocal = true;
    if (!isAllocaAllocationInCurrentStackFrame(mo)) {
      externalDelta = getPreviousStackFrameDelta(mo);
      if (externalDelta == nullptr) {
        // allocation was made in previous stack frame that is not available,
        // treat as global
        isLocal = false;
      }
    }
  }

  if (libraryFunction.entered) {
    if (isLocal && externalDelta == nullptr) {
      // change is only to be made to delta of current stack frame
      return;
    }
  }

  // optimization for concrete offsets: only hash changed indices
  if (concreteOffset) {
    begin = concreteOffset->getZExtValue(64);
    if ((begin + bytes) < os.size) {
      end = begin + bytes;
    }
  }

  ref<ConstantExpr> base = mo.getBaseExpr();

  auto symref = getSymbolicReferences(mo);

  for (std::uint64_t i = begin; i < end; i++) {
    std::uint64_t baseAddress = base->getZExtValue(64);

    if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
      llvm::errs() << "[+" << i << "] ";
    }

    // add value of byte at offset to fingerprint
    ref<Expr> valExpr = os.read8(i);
    if (ConstantExpr *constant = dyn_cast<ConstantExpr>(valExpr)) {
      // concrete value
      fingerprint.updateUint8(1);

      // add base address + offset to fingerprint
      fingerprint.updateUint64(baseAddress + i);

      std::uint8_t value = constant->getZExtValue(8);
      fingerprint.updateUint8(value);
      if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
        llvm::errs() << "0x";
        llvm::errs().write_hex((int)value);
      }
    } else {
      // symbolic value
      fingerprint.updateUint8(2);

      // add base address + offset to fingerprint
      fingerprint.updateUint64(baseAddress + i);

      fingerprint.updateExpr(valExpr);
      if (increaseReferenceCount) {
        increaseExprReferenceCount(valExpr, symref);
      } else {
        decreaseExprReferenceCount(valExpr, symref);
      }
      if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
        llvm::errs() << ExprString(valExpr);
      }
    }

    if (isLocal) {
      if (externalDelta == nullptr) {
        // current stack frame
        fingerprint.applyToFingerprintStackFrameDelta();
      } else {
        // previous stack frame that is still available
        fingerprint.applyToFingerprintStackFrameDelta(*externalDelta);
      }
    } else {
      fingerprint.applyToFingerprint();
    }

    if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
      if (i % 10 == 9) {
        llvm::errs() << "\n";
      } else {
        llvm::errs() << " ";
      }
    }
  }
}

void MemoryState::applyLocalFragment(std::uint64_t threadID,
                                     std::size_t stackFrameIndex,
                                     const llvm::Instruction *inst,
                                     ref<Expr> value) {
  if (ConstantExpr *constant = dyn_cast<ConstantExpr>(value)) {
    // concrete value
    fingerprint.updateUint8(3);
    fingerprint.updateUint64(threadID);
    fingerprint.updateUint64(stackFrameIndex);
    fingerprint.updateUint64(reinterpret_cast<std::uintptr_t>(inst));
    fingerprint.updateConstantExpr(*constant);
  } else {
    // symbolic value
    fingerprint.updateUint8(4);
    fingerprint.updateUint64(threadID);
    fingerprint.updateUint64(stackFrameIndex);
    fingerprint.updateUint64(reinterpret_cast<std::uintptr_t>(inst));
    fingerprint.updateExpr(value);
  }

  fingerprint.applyToTemporaryDelta();
}

void MemoryState::registerArgument(std::uint64_t threadID,
                                   std::size_t stackFrameIndex,
                                   const KFunction *kf,
                                   unsigned index,
                                   ref<Expr> value) {
  if (disableMemoryState) {
    return;
  }

  if (ConstantExpr *constant = dyn_cast<ConstantExpr>(value)) {
    // concrete value
    fingerprint.updateUint8(5);
    fingerprint.updateUint64(threadID);
    fingerprint.updateUint64(stackFrameIndex);
    fingerprint.updateUint64(reinterpret_cast<std::uintptr_t>(kf));
    fingerprint.updateUint64(index);
    fingerprint.updateConstantExpr(*constant);
  } else {
    // symbolic value
    fingerprint.updateUint8(6);
    fingerprint.updateUint64(threadID);
    fingerprint.updateUint64(stackFrameIndex);
    fingerprint.updateUint64(reinterpret_cast<std::uintptr_t>(kf));
    fingerprint.updateUint64(index);
    fingerprint.updateExpr(value);

    Thread &thread = executionState->getCurrentThreadReference();
    auto &sf = thread.stack.back();
    increaseExprReferenceCount(value, &sf.symbolicReferences);
  }

  fingerprint.applyToFingerprintStackFrameDelta();

  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    llvm::errs() << "MemoryState: adding argument " << index << " to function "
                 << reinterpret_cast<std::uintptr_t>(kf) << ": "
                 << ExprString(value) << "\n"
                 << " [fingerprint: " << fingerprint.getFingerprintAsString()
                 << "]\n";
  }
}

void MemoryState::enterBasicBlock(std::uint64_t threadID,
                                  std::size_t stackFrameIndex,
                                  const llvm::BasicBlock *dst,
                                  const llvm::BasicBlock *src) {

  if (disableMemoryState) {
    return;
  }

  if (src == nullptr) {
    assert(&(dst->getParent()->getEntryBlock()) == dst &&
           "dst is not an entry basic block");
    if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
      llvm::errs() << "MemoryState: Entering BasicBlock " << dst->getName()
                   << "\n";
    }
  } else {
    if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
      llvm::errs() << "MemoryState: Entering BasicBlock " << dst->getName()
                   << " (incoming edge: " << src->getName() << ")\n";
    }

    // unregister previous
    fingerprint.updateUint8(7);
    fingerprint.updateUint64(threadID);
    fingerprint.updateUint64(stackFrameIndex);
    fingerprint.updateUint64(reinterpret_cast<std::uintptr_t>(src));
    fingerprint.applyToFingerprintStackFrameDelta();
  }

  // register new basic block (program counter)
  fingerprint.updateUint8(7);
  fingerprint.updateUint64(threadID);
  fingerprint.updateUint64(stackFrameIndex);
  fingerprint.updateUint64(reinterpret_cast<std::uintptr_t>(dst));
  fingerprint.applyToFingerprintStackFrameDelta();
}

bool MemoryState::enterListedFunction(llvm::Function *f) {
  if (listedFunction.entered) {
    // we can only enter one listed function at a time
    // (we do not need to register additional functions calls by the entered
    // function such as printf)
    return false;
  }

  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    llvm::errs() << "MemoryState: entering listed function: "
                 << f->getName() << "\n";
  }

  listedFunction.entered = true;
  listedFunction.function = f;

  updateDisableMemoryState();

  return true;
}

void MemoryState::registerPushFrame(std::uint64_t threadID,
                                    std::size_t stackFrameIndex,
                                    const KFunction *callee,
                                    const KInstruction *caller) {
  // IMPORTANT: has to be called after state.pushFrame()
  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    llvm::errs() << "MemoryState: PUSHFRAME\n";
  }

  Thread &thread = executionState->getCurrentThreadReference();
  // second but last stack frame
  StackFrame &sf = thread.stack.at(thread.stack.size() - 2);
  sf.fingerprintDelta = fingerprint.getStackFrameDelta();

  // record alloca allocations and changes for this new stack frame separately
  // from those of other stack frames (without removing the latter)
  fingerprint.applyAndResetStackFrameDelta();

  // register stack frame
  fingerprint.updateUint8(8);
  fingerprint.updateUint64(threadID);
  fingerprint.updateUint64(stackFrameIndex);
  fingerprint.updateUint64(reinterpret_cast<std::uintptr_t>(caller));
  fingerprint.updateUint64(reinterpret_cast<std::uintptr_t>(callee));
  fingerprint.applyToFingerprintStackFrameDelta();

  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    llvm::errs() << "Fingerprint: " << fingerprint.getFingerprintAsString()
                 << "\n";
  }
}

void MemoryState::registerPopFrame(std::uint64_t threadID,
                                   const llvm::BasicBlock *returningBB,
                                   const llvm::BasicBlock *callerBB) {
  // IMPORTANT: has to be called prior to state.popFrame()

  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    llvm::errs() << "MemoryState: POPFRAME\n"
                 << "Fingerprint: " << fingerprint.getFingerprintAsString()
                 << "\n";
  }

  Thread &thread = executionState->getCurrentThreadReference();

  if (thread.stack.size() > 0) {
    // subtract symbolic references of stackframe delta that is to be removed
    for (auto s : thread.stack.back().symbolicReferences) {
      symbolicReferences[s.first] -= s.second;
    }

    std::string previousDelta;
    if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
        previousDelta = fingerprint.getStackFrameDeltaAsString();
    }

    // remove changes only accessible to stack frame that is to be left
    StackFrame &sf = thread.stack.at(thread.stack.size() - 2);
    fingerprint.discardStackFrameDelta();
    fingerprint.setStackFrameDeltaToPreviousValue(sf.fingerprintDelta);

    if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
      llvm::errs() << "removing stack frame delta: " << previousDelta
                   << "\nFingerprint: " << fingerprint.getFingerprintAsString()
                   << "\n";
    }
  } else {
    // no stackframe left to pop

    if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
      llvm::errs() << "no stackframe left in trace\n";
    }
  }
}


class ExprArrayCounter : public ExprVisitor {
public:
  MemoryState::sym_ref_map_t references;

public:
  ExprArrayCounter() = default;

  Action visitRead(const ReadExpr &e) {
    // this actually counts the number of bytes referenced,
    // as each ReadExpr represents a one byte read

    // root array
    const Array *arr = e.updates.root;
    ++references[arr];

    // symbolic index
    if (!isa<ConstantExpr>(e.index))
      visit(e.index);

    // update list
    auto update = e.updates.head;
    for (; update != nullptr; update=update->next) {
      if (!isa<ConstantExpr>(update->index))
        visit(update->index);
      if (!isa<ConstantExpr>(update->value))
        visit(update->value);
    }

    // only child node is index, which is already handled
    return Action::skipChildren();
  }
};


MemoryFingerprint::fingerprint_t MemoryState::getFingerprint() {
  // save symbolic references for easy restoration
  sym_ref_map_t saveSymbolicReferences = symbolicReferences;

  // include live locals of all existing (non-exited) threads
  for (auto it : executionState->threads) {
    auto threadID = it.first;
    const Thread &thread = it.second;
    if (thread.state == Thread::ThreadState::EXITED)
      continue;

    assert(thread.pc->info->getLiveLocals() != nullptr);
    for (const KInstruction *ki : *thread.pc->info->getLiveLocals()) {
      ref<Expr> value = thread.stack.back().locals[ki->dest].value;
      if (value.isNull())
        continue;

      applyLocalFragment(threadID, thread.stack.size() - 1, ki->inst, value);

      if (!isa<ConstantExpr>(value))
        increaseExprReferenceCount(value);
    }
  }

  std::vector<ref<Expr>> expressions;
  for (auto expr : executionState->constraints) {
    expressions.push_back(expr);
  }
  std::sort(expressions.begin(), expressions.end(), [](ref<Expr> a, ref<Expr> b) {
    auto aHash = a->hash();
    auto bHash = b->hash();
    if (aHash != bHash) {
      return aHash < bHash;
    } else {
      return a < b;
    }
  });

  // map all expressions to arrays that they constrain
  std::unordered_map<const Array *, std::set<ref<Expr>>> constraintsMap;
  for (auto expr : expressions) {
    ExprArrayCounter counter;
    counter.visit(expr);
    for (auto s : counter.references) {
      // exclude all arrays that have reference count 0
      if (symbolicReferences.count(s.first) && symbolicReferences[s.first] != 0) {
        constraintsMap[s.first].insert(expr);
      }
    }
  }


  // TODO: transitive closure


  if (!constraintsMap.empty()) {
    Thread &thread = executionState->getCurrentThreadReference();
    fingerprint.updateUint8(10);
    fingerprint.updateUint64(thread.tid);
    for (auto v : constraintsMap) {
      for (ref<Expr> expr : v.second) {
        fingerprint.updateExpr(expr);
      }
    }
    fingerprint.applyToTemporaryDelta();
  }

  auto result = fingerprint.getFingerprint();

  // cleanup
  fingerprint.discardTemporaryDelta();
  symbolicReferences = std::move(saveSymbolicReferences);

  return result;
}

void MemoryState::increaseExprReferenceCount(ref<Expr> expr,
                                             sym_ref_map_t *references) {
    ExprArrayCounter visitor;
    visitor.visit(expr);
    for (auto v : visitor.references) {
      if (references != nullptr)
        (*references)[v.first] += v.second;
      symbolicReferences[v.first] += v.second;
    }
}

void MemoryState::decreaseExprReferenceCount(ref<Expr> expr,
                                             sym_ref_map_t *references) {
    ExprArrayCounter visitor;
    visitor.visit(expr);
    for (auto v : visitor.references) {
      if (references != nullptr)
        (*references)[v.first] -= v.second;
      symbolicReferences[v.first] -= v.second;
    }
}

bool
MemoryState::isAllocaAllocationInCurrentStackFrame(const MemoryObject &mo) const {
  Thread &thread = executionState->getCurrentThreadReference();
  return (thread.stack.size() - 1 == mo.getStackframeIndex());
}

MemoryFingerprint::fingerprint_t *
MemoryState::getPreviousStackFrameDelta(const MemoryObject &mo) const {
  assert(!isAllocaAllocationInCurrentStackFrame(mo));

  Thread &thread = executionState->getCurrentThreadReference();

  size_t index = mo.getStackframeIndex();
  return &thread.stack.at(index).fingerprintDelta;
}


std::unordered_map<const Array *, std::uint64_t> *
MemoryState::getSymbolicReferences(const MemoryObject &mo) const {
  if (mo.isLocal) {
    Thread &thread = executionState->getCurrentThreadReference();
    size_t index = mo.getStackframeIndex();
    return &thread.stack.at(index).symbolicReferences;
  }
  return nullptr;
}

std::string MemoryState::ExprString(ref<Expr> expr) {
  std::string result;
  llvm::raw_string_ostream ostream(result);
  expr->print(ostream);
  ostream.flush();
  return result;
}

}
