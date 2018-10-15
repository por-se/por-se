#ifndef KLEE_MEMORYSTATE_H
#define KLEE_MEMORYSTATE_H

#include "InfiniteLoopDetectionFlags.h"
#include "Memory.h"
#include "MemoryFingerprint.h"

#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Function.h"

#include <cstdint>
#include <vector>
#include <unordered_map>

namespace llvm {
class BasicBlock;
class Function;
}

namespace klee {
class Array;
class ExecutionState;
class KFunction;
class KInstruction;
class KModule;

class MemoryState {
public:
  typedef std::unordered_map<const Array *, std::uint64_t> sym_ref_map_t;

private:
  MemoryState(const MemoryState &) = default;

  const ExecutionState *executionState = nullptr;

  MemoryFingerprint fingerprint;
  sym_ref_map_t symbolicReferences;

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
  bool isInListedFunction(llvm::Function *f) {
    return (listedFunction.entered && f == listedFunction.function);
  }

  bool enterLibraryFunction(llvm::Function *f);
  void leaveLibraryFunction();
  bool isInLibraryFunction(llvm::Function *f) {
    return (libraryFunction.entered && f == libraryFunction.function);
  }

  bool enterMemoryFunction(llvm::Function *f, ref<ConstantExpr> address,
    const MemoryObject *mo, const ObjectState *os, std::size_t bytes);
  bool isInMemoryFunction(llvm::Function *f) {
    return (memoryFunction.entered && f == memoryFunction.function);
  }
  void leaveMemoryFunction();

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

  void updateBasicBlockInfo(const llvm::BasicBlock *bb);
  void unregisterConsumedLocals(std::uint64_t threadID,
                                std::size_t stackFrameIndex,
                                const llvm::BasicBlock *bb);
  void unregisterKilledLocals(std::uint64_t threadID,
                              std::size_t stackFrameIndex,
                              const llvm::BasicBlock *dst,
                              const llvm::BasicBlock *src);

  void unregisterLocal(std::uint64_t threadID,
                       std::size_t stackFrameIndex,
                       const llvm::Instruction *inst,
                       ref<Expr> value,
                       bool force);

  void applyWriteFragment(ref<Expr> address, const MemoryObject &mo,
                          const ObjectState &os, std::size_t bytes,
                          bool increaseReferenceCount);
  void applyLocalFragment(std::uint64_t threadID, std::size_t stackFrameIndex,
                          const llvm::Instruction *inst, ref<Expr> value);

  bool isLocalLive(const llvm::Instruction *inst);

  bool isAllocaAllocationInCurrentStackFrame(const MemoryObject &mo) const;
  MemoryFingerprint::fingerprint_t *
  getPreviousStackFrameDelta(const MemoryObject &mo) const;

  std::unordered_map<const Array *, std::uint64_t> *
  getSymbolicReferences(const MemoryObject &mo) const;

  void increaseExprReferenceCount(ref<Expr> expr,
    sym_ref_map_t *references = nullptr);
  void decreaseExprReferenceCount(ref<Expr> expr,
    sym_ref_map_t *references = nullptr);

  KInstruction *getKInstruction(const llvm::Instruction* inst);
  KFunction *getKFunction(const llvm::BasicBlock *bb);
  ref<Expr> getLocalValue(const KInstruction *kinst);
  ref<Expr> getLocalValue(const llvm::Instruction *inst);
  void clearLocal(const KInstruction *kinst);
  void clearLocal(const llvm::Instruction *inst);

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
                       const llvm::Instruction *inst, ref<Expr> value) {
    unregisterLocal(threadID, stackFrameIndex, inst, value, false);
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


  MemoryFingerprint::fingerprint_t getFingerprint() {
    return fingerprint.getFingerprint();
  }
};
}

#endif
