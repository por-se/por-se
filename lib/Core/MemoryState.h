#ifndef KLEE_MEMORYSTATE_H
#define KLEE_MEMORYSTATE_H

#include "Memory.h"
#include "MemoryFingerprint.h"
#include "klee/StatePruningCmdLine.h"

#include <cstdint>
#include <vector>

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
private:
  MemoryState(const MemoryState &) = default;

  const ExecutionState *executionState = nullptr;

  MemoryFingerprint fingerprint;

  bool disableMemoryState = false;
  bool globalDisableMemoryState = false;

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

  static std::size_t externalFunctionCallCounter;

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

    if (DebugStatePruning) {
      llvm::errs() << "MemoryState: updating disableMemoryState: "
                   << "(listedFunction: " << listedFunction.entered << " || "
                   << "libraryFunction: " << libraryFunction.entered << " || "
                   << "memoryFunction: " << memoryFunction.entered << " || "
                   << "globalDisable: " << globalDisableMemoryState << ") "
                   << "= " << disableMemoryState << "\n";
    }
  }

  void applyWriteFragment(ref<Expr> address, const MemoryObject &mo,
                          const ObjectState &os, std::size_t bytes,
                          bool remove);

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

  std::size_t getFunctionListsLength() const {
    return MemoryState::outputFunctionsWhitelist.size()
        + MemoryState::libraryFunctionsList.size()
        + MemoryState::memoryFunctionsList.size();
  }

  std::size_t getFunctionListsCapacity() const {
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
  void registerWrite(const MemoryObject &mo, const ObjectState &os) {
    registerWrite(mo.getBaseExpr(), mo, os, os.size);
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

  void registerArgument(std::uint64_t threadID,
                        std::size_t stackFrameIndex,
                        const KFunction *kf,
                        unsigned index, ref<Expr> value);

  void registerExternalFunctionCall();

  void registerPushFrame(std::uint64_t threadID, std::size_t stackFrameIndex,
                         const KFunction *callee, const KInstruction *caller);
  void registerPopFrame(std::uint64_t threadID,
                        const llvm::BasicBlock *returningBB,
                        const llvm::BasicBlock *callerBB);

  MemoryFingerprint::value_t getFingerprint();
};
}

#endif
