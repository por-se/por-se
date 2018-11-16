// This is file is included from "MemoryFingerprint.h" and defines member
// functions of the base class MemoryFingerprintT.

#ifndef INCLUDE_MEMORYFINGERPRINT_BASE_H
static_assert(0, "DO NOT include this file directly!");
#endif

#include "klee/util/ExprVisitor.h"

namespace {
class ExprArrayCounter : public klee::ExprVisitor {
public:
  std::unordered_map<const klee::Array *, std::uint64_t> references;

public:
  ExprArrayCounter() = default;

  Action visitRead(const klee::ReadExpr &e) {
    // this actually counts the number of bytes referenced,
    // as each ReadExpr represents a one byte read

    // root array
    const klee::Array *arr = e.updates.root;
    ++references[arr];

    // symbolic index
    if (!isa<klee::ConstantExpr>(e.index))
      visit(e.index);

    // update list
    auto update = e.updates.head;
    for (; update != nullptr; update = update->next) {
      if (!isa<klee::ConstantExpr>(update->index))
        visit(update->index);
      if (!isa<klee::ConstantExpr>(update->value))
        visit(update->value);
    }

    // only child node is index, which is already handled
    return Action::skipChildren();
  }
};

} // namespace

namespace klee {

template <typename D, std::size_t S>
void MemoryFingerprintT<D, S>::updateExpr(const ref<Expr> expr) {
  bufferContainsSymbolic = true;
  getDerived().updateExpr_impl(expr);

  ExprArrayCounter visitor;
  visitor.visit(expr);
  for (auto v : visitor.references) {
    bufferSymbolicReferences[v.first] += v.second;
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
  executeXOR(fingerprintValue, buffer);
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
  executeXOR(fingerprintValue, buffer);
  getDerived().clearHash();

  if (bufferContainsSymbolic) {
    for (auto s : bufferSymbolicReferences) {
      symbolicReferences[s.first] -= s.second;
    }
    resetBufferRefCount();
  }
}

template <typename D, std::size_t S>
void MemoryFingerprintT<D, S>::addToFingerprintAndDelta(
    MemoryFingerprintDelta &delta) {
  getDerived().generateHash();
  executeXOR(delta.fingerprintValue, buffer);
  executeXOR(fingerprintValue, buffer);
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
  executeXOR(delta.fingerprintValue, buffer);
  executeXOR(fingerprintValue, buffer);
  getDerived().clearHash();

  if (bufferContainsSymbolic) {
    for (auto s : bufferSymbolicReferences) {
      symbolicReferences[s.first] -= s.second;
      delta.symbolicReferences[s.first] -= s.second;
    }
    resetBufferRefCount();
  }
}

template <typename D, std::size_t S>
void MemoryFingerprintT<D, S>::addToDeltaOnly(MemoryFingerprintDelta &delta) {
  getDerived().generateHash();
  executeXOR(delta.fingerprintValue, buffer);
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
  executeXOR(delta.fingerprintValue, buffer);
  getDerived().clearHash();

  if (bufferContainsSymbolic) {
    for (auto s : bufferSymbolicReferences) {
      delta.symbolicReferences[s.first] -= s.second;
    }
    resetBufferRefCount();
  }
}

template <typename D, std::size_t S>
void MemoryFingerprintT<D, S>::addDelta(MemoryFingerprintDelta &delta) {
  executeXOR(fingerprintValue, delta.fingerprintValue);

  for (auto s : delta.symbolicReferences) {
    symbolicReferences[s.first] += s.second;
  }
}

template <typename D, std::size_t S>
void MemoryFingerprintT<D, S>::removeDelta(MemoryFingerprintDelta &delta) {
  executeXOR(fingerprintValue, delta.fingerprintValue);

  for (auto s : delta.symbolicReferences) {
    symbolicReferences[s.first] -= s.second;
  }
}

template <typename D, std::size_t S>
typename MemoryFingerprintT<D, S>::value_t
MemoryFingerprintT<D, S>::getFingerprint(std::vector<ref<Expr>> &expressions) {
  MemoryFingerprintDelta temp;

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

  // map all expressions to arrays that they constrain
  std::unordered_map<const Array *, std::set<ref<Expr>>> constraintsMap;
  for (auto expr : expressions) {
    ExprArrayCounter counter;
    counter.visit(expr);
    for (auto s : counter.references) {
      // exclude all arrays that have reference count 0
      if (symbolicReferences.count(s.first) &&
          symbolicReferences[s.first] != 0) {
        constraintsMap[s.first].insert(expr);
      }
    }
  }

  // TODO: transitive closure

  if (!constraintsMap.empty()) {
    getDerived().updateUint8(10);
    for (auto v : constraintsMap) {
      for (ref<Expr> expr : v.second) {
        getDerived().updateExpr_impl(expr);
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
bool MemoryFingerprintT<Derived, hashSize>::updateBasicBlockFragment(
    std::uint64_t threadID, std::uint64_t sfIndex, const llvm::BasicBlock *bb) {
  getDerived().updateUint8(7);
  getDerived().updateUint64(threadID);
  getDerived().updateUint64(sfIndex);
  getDerived().updateUint64(reinterpret_cast<std::uintptr_t>(bb));
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
