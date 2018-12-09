// This is file is included from "MemoryFingerprint.h" and defines member
// functions of the base class MemoryFingerprintT.

#ifndef INCLUDE_MEMORYFINGERPRINT_BASE_H
static_assert(0, "DO NOT include this file directly!");
#endif

#include "klee/util/ExprHashMap.h"
#include "klee/util/ExprPPrinter.h"

namespace klee {

template <typename D, std::size_t S>
void MemoryFingerprintT<D, S>::updateExpr(const ref<Expr> expr) {
  llvm::raw_ostream &os = getDerived().updateOstream();
  std::unique_ptr<ExprPPrinter> p(ExprPPrinter::create(os));
  p->scan(expr);
  p->print(expr);
  os.flush();

  bufferContainsSymbolic = true;
  for (auto v : p->getUsedArrays()) {
    bufferSymbolicReferences[v] += 1;
  }
}

template <typename D, std::size_t S>
void MemoryFingerprintT<D, S>::updateConstantExpr(const ConstantExpr &expr) {
  if (expr.getWidth() <= 64) {
    std::uint64_t constantValue = expr.getZExtValue(64);
    getDerived().updateUint64(constantValue);
  } else {
    const llvm::APInt &value = expr.getAPValue();
    for (std::size_t i = 0; i != value.getNumWords(); i++) {
      std::uint64_t word = value.getRawData()[i];
      getDerived().updateUint64(word);
    }
  }
}

template <typename D, std::size_t S>
void MemoryFingerprintT<D, S>::addToFingerprint() {
  getDerived().generateHash();
  executeAdd(fingerprintValue, buffer);
  getDerived().clearHash();

  if (bufferContainsSymbolic) {
    for (auto s : bufferSymbolicReferences) {
      symbolicReferences[s.first] += s.second;
    }
    resetBufferRefCount();
  }
}

template <typename D, std::size_t S>
void MemoryFingerprintT<D, S>::removeFromFingerprint() {
  getDerived().generateHash();
  executeRemove(fingerprintValue, buffer);
  getDerived().clearHash();

  if (bufferContainsSymbolic) {
    for (auto s : bufferSymbolicReferences) {
      assert(symbolicReferences[s.first] >= s.second);
      symbolicReferences[s.first] -= s.second;
    }
    resetBufferRefCount();
  }
}

template <typename D, std::size_t S>
void MemoryFingerprintT<D, S>::addToFingerprintAndDelta(
    MemoryFingerprintDelta &delta) {
  getDerived().generateHash();
  executeAdd(delta.fingerprintValue, buffer);
  executeAdd(fingerprintValue, buffer);
  getDerived().clearHash();

  if (bufferContainsSymbolic) {
    for (auto s : bufferSymbolicReferences) {
      symbolicReferences[s.first] += s.second;
      delta.symbolicReferences[s.first] += s.second;
    }
    resetBufferRefCount();
  }
}

template <typename D, std::size_t S>
void MemoryFingerprintT<D, S>::removeFromFingerprintAndDelta(
    MemoryFingerprintDelta &delta) {
  getDerived().generateHash();
  executeRemove(delta.fingerprintValue, buffer);
  executeRemove(fingerprintValue, buffer);
  getDerived().clearHash();

  if (bufferContainsSymbolic) {
    for (auto s : bufferSymbolicReferences) {
      assert(symbolicReferences[s.first] >= s.second);
      assert(delta.symbolicReferences[s.first] >= s.second);
      symbolicReferences[s.first] -= s.second;
      delta.symbolicReferences[s.first] -= s.second;
    }
    resetBufferRefCount();
  }
}

template <typename D, std::size_t S>
void MemoryFingerprintT<D, S>::addToDeltaOnly(MemoryFingerprintDelta &delta) {
  getDerived().generateHash();
  executeAdd(delta.fingerprintValue, buffer);
  getDerived().clearHash();

  if (bufferContainsSymbolic) {
    for (auto s : bufferSymbolicReferences) {
      delta.symbolicReferences[s.first] += s.second;
    }
    resetBufferRefCount();
  }
}

template <typename D, std::size_t S>
void MemoryFingerprintT<D, S>::removeFromDeltaOnly(
    MemoryFingerprintDelta &delta) {
  getDerived().generateHash();
  executeRemove(delta.fingerprintValue, buffer);
  getDerived().clearHash();

  if (bufferContainsSymbolic) {
    for (auto s : bufferSymbolicReferences) {
      assert(delta.symbolicReferences[s.first] >= s.second);
      delta.symbolicReferences[s.first] -= s.second;
    }
    resetBufferRefCount();
  }
}

template <typename D, std::size_t S>
void MemoryFingerprintT<D, S>::addDelta(MemoryFingerprintDelta &delta) {
  executeAdd(fingerprintValue, delta.fingerprintValue);

  for (auto s : delta.symbolicReferences) {
    symbolicReferences[s.first] += s.second;
  }
}

template <typename D, std::size_t S>
void MemoryFingerprintT<D, S>::removeDelta(MemoryFingerprintDelta &delta) {
  executeRemove(fingerprintValue, delta.fingerprintValue);

  for (auto s : delta.symbolicReferences) {
    assert(symbolicReferences[s.first] >= s.second);
    symbolicReferences[s.first] -= s.second;
  }
}

template <typename D, std::size_t S>
typename MemoryFingerprintT<D, S>::value_t
MemoryFingerprintT<D, S>::getFingerprint(std::vector<ref<Expr>> &expressions) {
  std::set<const Array *> arraysReferenced;
  for (auto s : symbolicReferences) {
    if (s.second > 0)
      arraysReferenced.insert(s.first);
  }

  if (arraysReferenced.empty())
    return fingerprintValue;

  std::sort(expressions.begin(), expressions.end(),
            [](ref<Expr> a, ref<Expr> b) {
              auto aHash = a->hash();
              auto bHash = b->hash();
              if (aHash != bHash) {
                return aHash < bHash;
              } else {
                return a < b;
              }
            });

  // just needed to count array references using ExprPPrinter
  std::string dummyString;
  llvm::raw_string_ostream dummyOS(dummyString);

  // bidirectional mapping of expressions and arrays
  std::unordered_map<const Array *, ExprHashSet> constraintsMap;
  ExprHashMap<std::set<const Array *>> exprToArray;
  for (auto expr : expressions) {
    std::unique_ptr<ExprPPrinter> p(ExprPPrinter::create(dummyOS));
    p->scan(expr);

    for (auto s : p->getUsedArrays()) {
      constraintsMap[s].insert(expr);
      exprToArray[expr].insert(s);
    }
  }

  // transitive closure
  std::set<const Array *> newReferences = arraysReferenced;

  do {
    std::set<const Array *> tmp;
    std::swap(newReferences, tmp);
    assert(newReferences.empty());
    for (auto *a : tmp) {
      for (ref<Expr> c : constraintsMap[a]) {
        for (auto *b : exprToArray[c]) {
          if (!arraysReferenced.count(b)) {
            newReferences.insert(b);
            arraysReferenced.insert(b);
          }
        }
      }
    }
  } while (!newReferences.empty());

  // add path constraint to temporary delta
  MemoryFingerprintDelta temp;
  if (!arraysReferenced.empty()) {
    getDerived().updateUint8(10);
    for (auto v : arraysReferenced) {
      for (ref<Expr> expr : constraintsMap[v]) {
        llvm::raw_ostream &os = getDerived().updateOstream();
        ExprPPrinter::printSingleExpr(os, expr);
        os.flush();
      }
    }
    addToDeltaOnly(temp);
  }

  addDelta(temp);
  auto result = fingerprintValue;
  removeDelta(temp);

  return result;
}

template <typename D, std::size_t S>
typename MemoryFingerprintT<D, S>::value_t
MemoryFingerprintT<D, S>::getFingerprintWithDelta(
    std::vector<ref<Expr>> &expressions, MemoryFingerprintDelta &delta) {
  addDelta(delta);
  auto result = getFingerprint(expressions);
  removeDelta(delta);
  return result;
}

template <typename D, std::size_t S>
bool MemoryFingerprintT<D, S>::updateWriteFragment(std::uint64_t address,
                                                   ref<Expr> value) {
  if (ConstantExpr *constant = dyn_cast<ConstantExpr>(value)) {
    // concrete value
    getDerived().updateUint8(1);
    getDerived().updateUint64(address);
    std::uint8_t byte = constant->getZExtValue(8);
    getDerived().updateUint8(byte);
    return false;
  } else {
    // symbolic value
    getDerived().updateUint8(2);
    getDerived().updateUint64(address);
    getDerived().updateExpr(value);
    return true;
  }
}

template <typename Derived, std::size_t hashSize>
bool MemoryFingerprintT<Derived, hashSize>::updateLocalFragment(
    std::uint64_t threadID, std::uint64_t stackFrameIndex,
    const llvm::Instruction *inst, ref<Expr> value) {
  if (ConstantExpr *constant = dyn_cast<ConstantExpr>(value)) {
    // concrete value
    getDerived().updateUint8(3);
    getDerived().updateUint64(threadID);
    getDerived().updateUint64(stackFrameIndex);
    getDerived().updateUint64(reinterpret_cast<std::uintptr_t>(inst));
    getDerived().updateConstantExpr(*constant);
    return false;
  } else {
    // symbolic value
    getDerived().updateUint8(4);
    getDerived().updateUint64(threadID);
    getDerived().updateUint64(stackFrameIndex);
    getDerived().updateUint64(reinterpret_cast<std::uintptr_t>(inst));
    getDerived().updateExpr(value);
    return true;
  }
}

template <typename Derived, std::size_t hashSize>
bool MemoryFingerprintT<Derived, hashSize>::updateArgumentFragment(
    std::uint64_t threadID, std::uint64_t sfIndex, const KFunction *kf,
    std::uint64_t argumentIndex, ref<Expr> value) {
  if (ConstantExpr *constant = dyn_cast<ConstantExpr>(value)) {
    // concrete value
    getDerived().updateUint8(5);
    getDerived().updateUint64(threadID);
    getDerived().updateUint64(sfIndex);
    getDerived().updateUint64(reinterpret_cast<std::uintptr_t>(kf));
    getDerived().updateUint64(argumentIndex);
    getDerived().updateConstantExpr(*constant);
    return false;
  } else {
    // symbolic value
    getDerived().updateUint8(6);
    getDerived().updateUint64(threadID);
    getDerived().updateUint64(sfIndex);
    getDerived().updateUint64(reinterpret_cast<std::uintptr_t>(kf));
    getDerived().updateUint64(argumentIndex);
    getDerived().updateExpr(value);
    return true;
  }
}

template <typename Derived, std::size_t hashSize>
bool MemoryFingerprintT<Derived, hashSize>::updateProgramCounterFragment(
    std::uint64_t threadID, std::uint64_t sfIndex, const llvm::Instruction *i) {
  getDerived().updateUint8(7);
  getDerived().updateUint64(threadID);
  getDerived().updateUint64(sfIndex);
  getDerived().updateUint64(reinterpret_cast<std::uintptr_t>(i));
  return false;
}

template <typename Derived, std::size_t hashSize>
bool MemoryFingerprintT<Derived, hashSize>::updateFunctionFragment(
    std::uint64_t threadID, std::uint64_t sfIndex, const KFunction *callee,
    const KInstruction *caller) {
  getDerived().updateUint8(8);
  getDerived().updateUint64(threadID);
  getDerived().updateUint64(sfIndex);
  getDerived().updateUint64(reinterpret_cast<std::uintptr_t>(caller));
  getDerived().updateUint64(reinterpret_cast<std::uintptr_t>(callee));
  return false;
}

} // namespace klee
