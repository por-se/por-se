#include "MemoryState.h"

#include "AddressSpace.h"
#include "InfiniteLoopDetectionFlags.h"
#include "Memory.h"

#include "klee/ExecutionState.h"
#include "klee/Internal/Module/Cell.h"
#include "klee/Internal/Module/InstructionInfoTable.h"
#include "klee/Internal/Module/KModule.h"
#include "klee/Internal/Support/ErrorHandling.h"

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

KModule *MemoryState::kmodule = nullptr;
std::vector<llvm::Function *> MemoryState::outputFunctionsWhitelist;
std::vector<llvm::Function *> MemoryState::inputFunctionsBlacklist;
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

  // blacklist: input functions
  const char* inputFunctions[] = {
    // stdio.h
    "fopen", "freopen", "fread", "fgetc", "getc", "fgetwc", "getwc", "fgets",
    "fgetws", "getchar", "getwchar", "gets", "scanf", "fscanf", "sscanf",
    "wscanf", "fwscanf", "swscanf", "vscanf", "vfscanf", "vsscanf", "vwscanf",
    "vfwscanf", "vswscanf", "ftell", "ftello", "fseek", "fseeko", "fgetpos",
    "fsetpos", "feof", "ferror",

    // POSIX
    "open", "creat", "socket", "accept", "socketpair", "pipe", "opendir",
    "dirfd", "fileno", "read", "readv", "pread", "recv", "recvmsg", "lseek",
    "fstat", "fdopen", "ftruncate", "fsync", "fdatasync", "fstatvfs",
    "select", "pselect", "poll", "epoll", "flock", "fcntl", "lockf",

    // dirent.h
    "opendir", "readdir", "readdir_r", "telldir",

    // future POSIX
    "openat", "faccessat", "fstatat", "readlinkat", "fdopendir",

    // LFS
    "fgetpos64", "fopen64", "freopen64", "fseeko64", "fsetpos64", "ftello64",
    "fstat64", "lstat64", "open64", "readdir64", "stat64"
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
  initializeFunctionList(_kmodule, inputFunctions, inputFunctionsBlacklist);
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



void MemoryState::registerFunctionCall(llvm::Function *f,
                                       std::vector<ref<Expr>> &arguments) {
  if (globalDisableMemoryState) {
    // we only check for global disable and not for library or listed functions
    // as we assume that those will not call any input or output functions
    return;
  }

  if (std::binary_search(inputFunctionsBlacklist.begin(),
                         inputFunctionsBlacklist.end(),
                         f)) {
    if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
      llvm::errs() << "MemoryState: blacklisted input function call to "
                   << f->getName() << "()\n";
    }
    clearEverything();
    enterListedFunction(f);
  } else if (std::binary_search(outputFunctionsWhitelist.begin(),
                                outputFunctionsWhitelist.end(),
                                f)) {
    if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
      llvm::errs() << "MemoryState: whitelisted output function call to "
                   << f->getName() << "()\n";
    }
    enterListedFunction(f);
  } else if (std::binary_search(libraryFunctionsList.begin(),
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


void MemoryState::clearEverything() {
  trace.clear();
  fingerprint.discardEverything();
}

void MemoryState::registerExternalFunctionCall() {
  if (listedFunction.entered) {
    return;
  }

  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    llvm::errs() << "MemoryState: external function call\n";
  }

  // it is unknown whether control flow is changed by an external function, so
  // we cannot detect infinite loop iterations that started before this call
  trace.clear();
  fingerprint.discardEverything();
}

void MemoryState::registerWrite(ref<Expr> address, const MemoryObject &mo,
                                const ObjectState &os, std::size_t bytes) {
  if (disableMemoryState && !libraryFunction.entered) {
    // in case of library functions, we need to record changes to global memory
    // and previous allocas
    return;
  }

  ref<ConstantExpr> base = mo.getBaseExpr();

  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    llvm::errs() << "MemoryState: processing "
                 << (mo.isLocal ? "local " : "global ")
                 << "ObjectState at base address "
                 << ExprString(base) << "\n";
  }

  ref<Expr> offset = mo.getOffsetExpr(address);
  ConstantExpr *concreteOffset = dyn_cast<ConstantExpr>(offset);

  std::uint64_t begin = 0;
  std::uint64_t end = os.size;

  bool isLocal = false;
  MemoryFingerprint::fingerprint_t *externalDelta = nullptr;

  if (mo.isLocal) {
    isLocal = true;
    if (!trace.isAllocaAllocationInCurrentStackFrame(*executionState, mo)) {
      externalDelta = trace.getPreviousAllocaDelta(*executionState, mo);
      if (externalDelta == nullptr) {
        // allocation was made in previous stack frame that is not available
        // anymore due to an external function call
        isLocal = false;
      }
    }
  }

  if (libraryFunction.entered) {
    if (isLocal && externalDelta == nullptr) {
      // change is only to be made to allocaDelta of current stack frame
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
      if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
        llvm::errs() << ExprString(valExpr);
      }
    }

    if (isLocal) {
      if (externalDelta == nullptr) {
        // current stack frame
        fingerprint.applyToFingerprintAllocaDelta();
      } else {
        // previous stack frame that is still available
        fingerprint.applyToFingerprintAllocaDelta(*externalDelta);
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
  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    llvm::errs() << " [fingerprint: "
                 << fingerprint.getFingerprintAsString() << "]\n";
  }
}

bool MemoryState::isLocalLive(const llvm::Instruction *inst) {
  // all variables are considered live if live variable analysis is disabled
  if (InfiniteLoopDetectionDisableLiveVariableAnalysis)
    return true;

  updateBasicBlockInfo(inst->getParent());

  const llvm::Value *instValue = cast<llvm::Value>(inst);
  return (std::find(basicBlockInfo.liveRegisters.begin(),
                    basicBlockInfo.liveRegisters.end(),
                    instValue) != basicBlockInfo.liveRegisters.end());
}

void MemoryState::registerLocal(const KInstruction *target, ref<Expr> value) {
  if (disableMemoryState) {
    return;
  }

  if (value.isNull()) {
    return;
  }

  llvm::Instruction *inst = target->inst;

  if (!isLocalLive(target->inst))
    return;

  registerLocal(inst, value);

  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    llvm::errs() << "MemoryState: register local %" << target->inst->getName()
                 << ": " << ExprString(value)
                 << " [fingerprint: " << fingerprint.getFingerprintAsString()
                 << "]\n";
  }
}

void MemoryState::registerLocal(const llvm::Instruction *inst, ref<Expr> value)
{
  if (disableMemoryState) {
    return;
  }

  if (value.isNull()) {
    return;
  }

  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    llvm::errs() << "registerLocal(%" << inst->getName() << ", value)\n";
  }

  if (ConstantExpr *constant = dyn_cast<ConstantExpr>(value)) {
    // concrete value
    fingerprint.updateUint8(3);
    fingerprint.updateUint64(reinterpret_cast<std::uintptr_t>(inst));
    fingerprint.updateConstantExpr(*constant);
  } else {
    // symbolic value
    fingerprint.updateUint8(4);
    fingerprint.updateUint64(reinterpret_cast<std::uintptr_t>(inst));
    fingerprint.updateExpr(value);
  }

  fingerprint.applyToFingerprintLocalDelta();
}

void MemoryState::registerArgument(const KFunction *kf, unsigned index,
                                   ref<Expr> value) {
  if (disableMemoryState) {
    return;
  }

  if (ConstantExpr *constant = dyn_cast<ConstantExpr>(value)) {
    // concrete value
    fingerprint.updateUint8(5);
    fingerprint.updateUint64(reinterpret_cast<std::uintptr_t>(kf));
    fingerprint.updateUint64(index);
    fingerprint.updateConstantExpr(*constant);
  } else {
    // symbolic value
    fingerprint.updateUint8(6);
    fingerprint.updateUint64(reinterpret_cast<std::uintptr_t>(kf));
    fingerprint.updateUint64(index);
    fingerprint.updateExpr(value);
  }

  fingerprint.applyToFingerprintLocalDelta();

  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    llvm::errs() << "MemoryState: adding argument " << index << " to function "
                 << reinterpret_cast<std::uintptr_t>(kf) << ": "
                 << ExprString(value) << "\n"
                 << " [fingerprint: " << fingerprint.getFingerprintAsString()
                 << "]\n";
  }
}


void MemoryState::updateBasicBlockInfo(const llvm::BasicBlock *bb) {
  assert(!InfiniteLoopDetectionDisableLiveVariableAnalysis
         && "should not be called with live variable analysis disabled");

  if (basicBlockInfo.bb == bb)
    return;

  basicBlockInfo.bb = bb;
  basicBlockInfo.liveRegisters.clear();

  KFunction *kf = getKFunction(bb);
  auto liveset = kf->basicBlockValueLivenessInfo.at(bb).getLiveValues();
  basicBlockInfo.liveRegisters = std::vector<llvm::Value *>(liveset.begin(),
                                                            liveset.end());
}

void MemoryState::unregisterConsumedLocals(const llvm::BasicBlock *bb,
                                           bool writeToLocalDelta) {
  // This method is called after the execution of bb to clean up the local
  // delta, but also set locals to NULL within KLEE.
  // The parameter "writeToLocalDelta" can be set to false in order to omit
  // changes to a local delta that will be discarded immediately after.

  KFunction *kf = getKFunction(bb);
  auto consumed = kf->basicBlockValueLivenessInfo.at(bb).getConsumedValues();

  for (auto &c : consumed) {
    llvm::Instruction *inst = static_cast<llvm::Instruction *>(c);
    if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
      llvm::errs() << "MemoryState: Following variable (last written to "
                   << "in BasicBlock %" << bb->getName()
                   << ") is dead in any successor: "
                   << "%" << inst->getName() << "\n";
    }
    if (writeToLocalDelta) {
      // remove local from local delta
      unregisterLocal(inst);
    }
    // set local within KLEE to NULL to mark it as dead
    // this prevents us from unregistering this local twice
    clearLocal(inst);
  }
}


void MemoryState::enterBasicBlock(const llvm::BasicBlock *dst,
                                  const llvm::BasicBlock *src) {
  if (InfiniteLoopDetectionDisableLiveVariableAnalysis) {
    return;
  }

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
    fingerprint.updateUint64(reinterpret_cast<std::uintptr_t>(src));
    fingerprint.applyToFingerprintLocalDelta();

    unregisterConsumedLocals(src);
  }
  updateBasicBlockInfo(dst);

  // register new basic block (program counter)
  fingerprint.updateUint8(7);
  fingerprint.updateUint64(reinterpret_cast<std::uintptr_t>(dst));
  fingerprint.applyToFingerprintLocalDelta();


  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    llvm::errs() << "MemoryState: The following variables are live "
                 << "at the end of BasicBlock " << dst->getName() << ": {";
    for (std::size_t i = 0; i < basicBlockInfo.liveRegisters.size(); ++i) {
      bool last = (i == basicBlockInfo.liveRegisters.size() - 1);
      llvm::Value *liveRegister = basicBlockInfo.liveRegisters.at(i);
      llvm::errs() << "%" << liveRegister->getName() << (last ? "" : ", ");
    }
    llvm::errs() << "}\n";
  }
}

void MemoryState::phiNodeProcessingCompleted(const llvm::BasicBlock *dst,
                                             const llvm::BasicBlock *src) {
  if (InfiniteLoopDetectionDisableLiveVariableAnalysis) {
    return;
  }

  if (disableMemoryState) {
    return;
  }

  unregisterKilledLocals(dst, src);
}

void MemoryState::unregisterKilledLocals(const llvm::BasicBlock *dst,
                                         const llvm::BasicBlock *src) {
  // kill registers based on outgoing edge (edge from src to dst)

  KFunction *kf = getKFunction(src);
  auto killed = kf->basicBlockValueLivenessInfo.at(src).getKilledValues(dst);

  // unregister and clear locals that are not live at the end of dst
  for (auto &k : killed) {
    llvm::Instruction *inst = static_cast<llvm::Instruction *>(k);
    if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
      llvm::errs() << "MemoryState: Following variable (last accessed "
                   << "in BasicBlock %" << src->getName()
                   << ") is dead in BasicBlock %" << dst->getName()
                   << " (after evaluation of PHI nodes, if any)"
                   << ": %" << inst->getName() << "\n";
    }
    // remove local from local delta
    unregisterLocal(inst);
    // set local within KLEE to NULL to mark it as dead
    // this prevents us from unregistering this local twice
    clearLocal(inst);
  }
}

KInstruction *MemoryState::getKInstruction(const llvm::BasicBlock* bb) {
  KFunction *kf = getKFunction(bb);
  unsigned entry = kf->basicBlockEntry[const_cast<llvm::BasicBlock *>(bb)];
  return kf->instructions[entry];
}

KInstruction *MemoryState::getKInstruction(const llvm::Instruction* inst) {
  // FIXME: ugly hack
  llvm::BasicBlock *bb = const_cast<llvm::BasicBlock *>(inst->getParent());
  if (bb != nullptr) {
    KFunction *kf = getKFunction(bb);
    if (kf != nullptr) {
      unsigned entry = kf->basicBlockEntry[bb];
      while ((entry + 1) < kf->numInstructions
             && kf->instructions[entry]->inst != inst)
      {
        entry++;
      }
      return kf->instructions[entry];
    }
  }
  return nullptr;
}

KFunction *MemoryState::getKFunction(const llvm::BasicBlock *bb) {
  llvm::Function *f = const_cast<llvm::Function *>(bb->getParent());
  assert(f != nullptr && "failed to retrieve Function for BasicBlock");
  KFunction *kf = kmodule->functionMap[f];
  assert(kf != nullptr && "failed to retrieve KFunction");
  return kf;
}

ref<Expr> MemoryState::getLocalValue(const KInstruction *kinst) {
  return executionState->stack.back().locals[kinst->dest].value;
}

ref<Expr> MemoryState::getLocalValue(const llvm::Instruction *inst) {
  KInstruction *kinst = getKInstruction(inst);
  if (kinst != nullptr) {
    return getLocalValue(kinst);
  }
  return nullptr;
}

void MemoryState::clearLocal(const KInstruction *kinst) {
  executionState->stack.back().locals[kinst->dest].value = nullptr;
  assert(getLocalValue(kinst).isNull());
}

void MemoryState::clearLocal(const llvm::Instruction *inst) {
  KInstruction *kinst = getKInstruction(inst);
  if (kinst != nullptr) {
    clearLocal(kinst);
  }
  assert(getLocalValue(inst).isNull());
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

void MemoryState::leaveListedFunction() {
  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    llvm::errs() << "MemoryState: leaving listed function: "
                 << listedFunction.function->getName() << "\n";
  }

  listedFunction.entered = false;
  listedFunction.function = nullptr;

  updateDisableMemoryState();
}

bool MemoryState::isInListedFunction(llvm::Function *f) {
  return (listedFunction.entered && f == listedFunction.function);
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

bool MemoryState::isInLibraryFunction(llvm::Function *f) {
  return (libraryFunction.entered && f == libraryFunction.function);
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

bool MemoryState::isInMemoryFunction(llvm::Function *f) {
  return (memoryFunction.entered && f == memoryFunction.function);
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

void MemoryState::registerPushFrame(const KFunction *kf) {
  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    llvm::errs() << "MemoryState: PUSHFRAME\n";
  }

  trace.registerEndOfStackFrame(kf,
                                fingerprint.getLocalDelta(),
                                fingerprint.getAllocaDelta());

  // make locals and arguments "invisible"
  fingerprint.discardLocalDelta();
  // record alloca allocations and changes for this new stack frame separately
  // from those of other stack frames (without removing the latter)
  fingerprint.applyAndResetAllocaDelta();

  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    llvm::errs() << "Fingerprint: " << fingerprint.getFingerprintAsString()
                 << "\n";
  }
}

void MemoryState::registerPopFrame(const llvm::BasicBlock *returningBB,
                                   const llvm::BasicBlock *callerBB) {
  // IMPORTANT: has to be called prior to state.popFrame()

  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    llvm::errs() << "MemoryState: POPFRAME\n"
                 << "Fingerprint: " << fingerprint.getFingerprintAsString()
                 << "\n";
  }

  if (trace.getNumberOfStackFrames() > 0) {
    if (!InfiniteLoopDetectionDisableLiveVariableAnalysis) {
      // Even though the local delta is removed in the next step, we have to
      // clear consumed locals within KLEE to be able to determine which
      // variable has already been registered during another call to the
      // function we are currently leaving.
      unregisterConsumedLocals(returningBB, false);
    }

    MemoryTrace::StackFrameEntry sfe = trace.popFrame();

    // remove locals and arguments of stack frame that is to be left
    fingerprint.discardLocalDelta();
    // set local delta to fingerprint local delta of stack frame that is to be
    // entered to make locals and arguments "visible" again
    fingerprint.setLocalDelta(sfe.fingerprintLocalDelta);

    // remove allocas allocated in stack frame that is to be left
    fingerprint.discardAllocaDelta();
    // initialize alloca delta with previous fingerprint alloca delta which
    // contains information on allocas allocated in the stack frame that is to
    // be entered
    fingerprint.setAllocaDeltaToPreviousValue(sfe.fingerprintAllocaDelta);

    if (!InfiniteLoopDetectionDisableLiveVariableAnalysis) {
      updateBasicBlockInfo(callerBB);
    }

    if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
      llvm::errs() << "reapplying local delta: "
                   << fingerprint.getLocalDeltaAsString()
                   << "\nreapplying alloca delta: "
                   << fingerprint.getAllocaDeltaAsString()
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


std::string MemoryState::ExprString(ref<Expr> expr) {
  std::string result;
  llvm::raw_string_ostream ostream(result);
  expr->print(ostream);
  ostream.flush();
  return result;
}

}
