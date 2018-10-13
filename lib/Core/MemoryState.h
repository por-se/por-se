#ifndef KLEE_MEMORYSTATE_H
#define KLEE_MEMORYSTATE_H

#include "InfiniteLoopDetectionFlags.h"
#include "Memory.h"
#include "MemoryFingerprint.h"

#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/KModule.h"

#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Function.h"

#include <cstdint>
#include <vector>

namespace llvm {
class BasicBlock;
class Function;
}

namespace klee {
class ExecutionState;

class MemoryState {

private:
  MemoryState(const MemoryState &) = default;

  MemoryFingerprint fingerprint;
  const ExecutionState *executionState = nullptr;

  // klee_enable_memory_state() is inserted by KLEE before executing the entry
  // point chosen by the user. Thus, the initialization of (uc)libc or POSIX
  // runtime are not analyzed (we asume them to be free of liveness violations).
  bool disableMemoryState = true;
  bool globalDisableMemoryState = true;

  struct listedFunction {
    bool entered = false;
    llvm::Function *function = nullptr;
  } listedFunction;

  struct libraryFunction {
    bool entered = false;
    llvm::Function *function = nullptr;
  } libraryFunction;

  struct memoryFunction {
    bool entered = false;
    llvm::Function *function = nullptr;
    ref<ConstantExpr> address;
    const MemoryObject *mo = nullptr;
    std::size_t bytes = 0;
  } memoryFunction;

  struct basicBlockInfo {
    const llvm::BasicBlock *bb = nullptr;
    std::vector<llvm::Value *> liveRegisters;
  } basicBlockInfo;

  static size_t externalFunctionCallCounter;

  static KModule *kmodule;
  static std::vector<llvm::Function *> outputFunctionsWhitelist;
  static std::vector<llvm::Function *> libraryFunctionsList;
  static std::vector<llvm::Function *> memoryFunctionsList;

  template <std::size_t array_size>
  static void initializeFunctionList(KModule *kmodule,
                                     const char* (& functions)[array_size],
                                     std::vector<llvm::Function *> &list);

  static std::string ExprString(ref<Expr> expr);

  bool enterListedFunction(llvm::Function *f);
  void leaveListedFunction();
  bool isInListedFunction(llvm::Function *f);

  bool enterLibraryFunction(llvm::Function *f);
  void leaveLibraryFunction();
  bool isInLibraryFunction(llvm::Function *f);

  bool enterMemoryFunction(llvm::Function *f, ref<ConstantExpr> address,
    const MemoryObject *mo, const ObjectState *os, std::size_t bytes);
  bool isInMemoryFunction(llvm::Function *f);
  void leaveMemoryFunction();

  void updateBasicBlockInfo(const llvm::BasicBlock *bb);
  KInstruction *getKInstruction(const llvm::BasicBlock* bb);
  KInstruction *getKInstruction(const llvm::Instruction* inst);
  KFunction *getKFunction(const llvm::BasicBlock *bb);
  ref<Expr> getLocalValue(const KInstruction *kinst);
  ref<Expr> getLocalValue(const llvm::Instruction *inst);
  void clearLocal(const KInstruction *kinst);
  void clearLocal(const llvm::Instruction *inst);

  void unregisterConsumedLocals(std::uint64_t threadID,
                                std::size_t stackFrameIndex,
                                const llvm::BasicBlock *bb);
  void unregisterKilledLocals(std::uint64_t threadID,
                              std::size_t stackFrameIndex,
                              const llvm::BasicBlock *dst,
                              const llvm::BasicBlock *src);

  void applyWriteFragment(ref<Expr> address, const MemoryObject &mo,
                          const ObjectState &os, std::size_t bytes);
  void applyLocalFragment(std::uint64_t threadID, std::size_t stackFrameIndex,
                          const llvm::Instruction *inst, ref<Expr> value);

  bool isLocalLive(const llvm::Instruction *inst);
  bool shouldRegisterLocal(const llvm::Instruction *inst);

  void updateDisableMemoryState() {
    disableMemoryState = listedFunction.entered || libraryFunction.entered || memoryFunction.entered || globalDisableMemoryState;

    if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
      llvm::errs() << "MemoryState: updating disableMemoryState: "
                   << "(listedFunction: " << listedFunction.entered << " || "
                   << "libraryFunction: " << libraryFunction.entered << " || "
                   << "memoryFunction: " << memoryFunction.entered << " || "
                   << "globalDisable: " << globalDisableMemoryState << ") "
                   << "= " << disableMemoryState << "\n";
    }
  }

public:
  MemoryState() = delete;
  MemoryState& operator=(const MemoryState&) = delete;

  MemoryState(const ExecutionState *state) : executionState(state) {}
  MemoryState(const MemoryState &from, const ExecutionState *state)
    : MemoryState(from) {
    executionState = state;
 }

  void disable() {
    globalDisableMemoryState = true;
    updateDisableMemoryState();
  }

  void enable() {
    globalDisableMemoryState = false;
    updateDisableMemoryState();
  }

  bool isEnabled() {
    return !disableMemoryState;
  }

  size_t getFunctionListsLength() const {
    return MemoryState::outputFunctionsWhitelist.size()
        + MemoryState::libraryFunctionsList.size()
        + MemoryState::memoryFunctionsList.size();
  }

  size_t getFunctionListsCapacity() const {
    return MemoryState::outputFunctionsWhitelist.capacity()
        + MemoryState::libraryFunctionsList.capacity()
        + MemoryState::memoryFunctionsList.capacity();
  }

  static void setKModule(KModule *kmodule);

  void registerFunctionCall(llvm::Function *f,
                            std::vector<ref<Expr>> &arguments);
  void registerFunctionRet(llvm::Function *f);

  void registerWrite(ref<Expr> address, const MemoryObject &mo,
                     const ObjectState &os, std::size_t bytes);
  void registerWrite(ref<Expr> address, const MemoryObject &mo,
                     const ObjectState &os) {
    registerWrite(address, mo, os, os.size);
  }
  void unregisterWrite(ref<Expr> address, const MemoryObject &mo,
                       const ObjectState &os, std::size_t bytes);
  void unregisterWrite(ref<Expr> address, const MemoryObject &mo,
                                          const ObjectState &os) {
    unregisterWrite(address, mo, os, os.size);
  }
  void unregisterWrite(const MemoryObject &mo, const ObjectState &os) {
    unregisterWrite(mo.getBaseExpr(), mo, os, os.size);
  }

  void registerLocal(std::uint64_t threadID, std::size_t stackFrameIndex,
                     const llvm::Instruction *inst, ref<Expr> value);
  void unregisterLocal(std::uint64_t threadID, std::size_t stackFrameIndex,
                       const llvm::Instruction *inst, ref<Expr> value);
  void unregisterLocal(std::uint64_t threadID, std::size_t stackFrameIndex,
                       const llvm::Instruction *inst) {
    ref<Expr> value = getLocalValue(inst);
    unregisterLocal(threadID, stackFrameIndex, inst, value);
  }

  void registerArgument(std::uint64_t threadID,
                        std::size_t stackFrameIndex,
                        const KFunction *kf,
                        unsigned index, ref<Expr> value);

  void registerExternalFunctionCall();

  void enterBasicBlock(std::uint64_t threadID,
                       std::size_t stackFrameIndex,
                       const llvm::BasicBlock *dst,
                       const llvm::BasicBlock *src = nullptr);
  void phiNodeProcessingCompleted(std::uint64_t threadID,
                                  std::size_t stackFrameIndex,
                                  const llvm::BasicBlock *dst,
                                  const llvm::BasicBlock *src);

  void registerPushFrame(std::uint64_t threadID, size_t stackFrameIndex,
                         const KFunction *callee, const llvm::Instruction *caller);
  void registerPopFrame(std::uint64_t threadID,
                        const llvm::BasicBlock *returningBB,
                        const llvm::BasicBlock *callerBB);

  bool isAllocaAllocationInCurrentStackFrame(const MemoryObject &mo);

  MemoryFingerprint::fingerprint_t *
  getPreviousStackFrameDelta(const MemoryObject &mo);

  MemoryFingerprint::fingerprint_t getFingerprint();
};
}

#endif
