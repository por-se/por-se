#ifndef KLEE_MEMORYFINGERPRINT_H
#define KLEE_MEMORYFINGERPRINT_H

#include "klee/Config/config.h"
#include "klee/Expr.h"

#ifndef __cpp_rtti
// stub for typeid to use CryptoPP without RTTI
template <typename T> const std::type_info &FakeTypeID(void) {
  assert(0 && "CryptoPP tries to use typeid()");
}
#define typeid(a) FakeTypeID < a > ()
#endif
#include <cryptopp/blake2.h>
#ifndef __cpp_rtti
#undef typeid
#endif

#include <array>
#include <iomanip>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

namespace llvm {
class BasicBlock;
class Instruction;
} // namespace llvm

namespace klee {
class KFunction;
class KInstruction;

class MemoryFingerprintDelta;

class MemoryFingerprint_CryptoPP_BLAKE2b;
class MemoryFingerprint_Dummy;

// Set default implementation
using MemoryFingerprint = MemoryFingerprint_CryptoPP_BLAKE2b;

template <typename Derived, std::size_t hashSize> class MemoryFingerprintT {

protected:
  using hash_t = std::array<std::uint8_t, hashSize>;
  using dummy_t = std::set<std::string>;

public:
  typedef
      typename std::conditional<hashSize == 0, dummy_t, hash_t>::type value_t;

private:
  Derived &getDerived() { return *(static_cast<Derived *>(this)); }

  value_t fingerprintValue{};
  std::unordered_map<const Array *, std::uint64_t> symbolicReferences;

  template <typename T, typename std::enable_if<std::is_same<T, hash_t>::value,
                                                int>::type = 0>
  inline void executeXOR(T &dst, const T &src) {
    for (std::size_t i = 0; i < hashSize; ++i) {
      dst[i] ^= src[i];
    }
  }

  template <typename T, typename std::enable_if<std::is_same<T, dummy_t>::value,
                                                int>::type = 0>
  inline void executeXOR(T &dst, const T &src) {
    for (auto &elem : src) {
      auto pos = dst.find(elem);
      if (pos == dst.end()) {
        dst.insert(elem);
      } else {
        dst.erase(pos);
      }
    }
  }

protected:
  // buffer that holds current hash after calling generateHash()
  value_t buffer = {};

private:
  // information on what went into buffer
  bool bufferContainsSymbolic = false;
  std::unordered_map<const Array *, std::uint64_t> bufferSymbolicReferences;

  void resetBufferRefCount() {
    bufferContainsSymbolic = false;
    bufferSymbolicReferences.clear();
  }

public:
  MemoryFingerprintT() = default;
  MemoryFingerprintT(const MemoryFingerprintT &) = default;

  void addToFingerprint();
  void removeFromFingerprint();

  void addToFingerprintAndDelta(MemoryFingerprintDelta &delta);
  void removeFromFingerprintAndDelta(MemoryFingerprintDelta &delta);
  void addToDeltaOnly(MemoryFingerprintDelta &delta);
  void removeFromDeltaOnly(MemoryFingerprintDelta &delta);
  void addDelta(MemoryFingerprintDelta &delta);
  void removeDelta(MemoryFingerprintDelta &delta);

  value_t getFingerprint(std::vector<ref<Expr>> &expressions);

  value_t getFingerprintWithDelta(std::vector<ref<Expr>> &expressions,
                                  MemoryFingerprintDelta &delta);

  void updateExpr(const ref<Expr> expr);

  void updateConstantExpr(const ConstantExpr &expr);

  template <typename T, typename std::enable_if<std::is_same<T, hash_t>::value,
                                                int>::type = 0>
  static std::string toString(const T &fingerprintValue) {
    std::stringstream result;
    for (const auto byte : fingerprintValue) {
      result << std::hex << std::setfill('0') << std::setw(2);
      result << static_cast<unsigned int>(byte);
    }
    return result.str();
  }

  template <typename T, typename std::enable_if<std::is_same<T, dummy_t>::value,
                                                int>::type = 0>
  static std::string toString(const T &fingerprintValue) {
    return Derived::toString_impl(fingerprintValue);
  }

  std::string getFingerprintAsString() {
    // FIXME: this has to be with referenced expressions
    return toString(fingerprintValue);
  }

  bool updateWriteFragment(std::uint64_t address, ref<Expr> value);
  bool updateLocalFragment(std::uint64_t threadID,
                           std::uint64_t stackFrameIndex,
                           const llvm::Instruction *inst, ref<Expr> value);
  bool updateArgumentFragment(std::uint64_t threadID, std::uint64_t sfIndex,
                              const KFunction *kf, std::uint64_t argumentIndex,
                              ref<Expr> value);
  bool updateBasicBlockFragment(std::uint64_t threadID, std::uint64_t sfIndex,
                                const llvm::BasicBlock *bb);
  bool updateFunctionFragment(std::uint64_t threadID, std::uint64_t sfIndex,
                              const KFunction *callee,
                              const KInstruction *caller);
};

class MemoryFingerprint_CryptoPP_BLAKE2b
    : public MemoryFingerprintT<MemoryFingerprint_CryptoPP_BLAKE2b, 32> {
  friend class MemoryFingerprintT<MemoryFingerprint_CryptoPP_BLAKE2b, 32>;

private:
  CryptoPP::BLAKE2b blake2b = CryptoPP::BLAKE2b(false, 32);
  void generateHash();
  void clearHash();

public:
  void updateUint8(const std::uint8_t value);
  void updateUint64(const std::uint64_t value);
  void updateExpr_impl(ref<Expr> expr);
};

class MemoryFingerprint_Dummy
    : public MemoryFingerprintT<MemoryFingerprint_Dummy, 0> {
  friend class MemoryFingerprintT<MemoryFingerprint_Dummy, 0>;

private:
  std::string current;
  bool first = true;
  void generateHash();
  void clearHash();
  static std::string toString_impl(dummy_t fingerprintValue);

public:
  void updateUint8(const std::uint8_t value);
  void updateUint64(const std::uint64_t value);
  void updateExpr_impl(ref<Expr> expr);
};

// NOTE: MemoryFingerprint needs to be a complete type
class MemoryFingerprintDelta {
private:
  friend class MemoryState;

  template <typename Derived, std::size_t hashSize>
  friend void MemoryFingerprintT<Derived, hashSize>::addToFingerprintAndDelta(
      MemoryFingerprintDelta &delta);

  template <typename Derived, std::size_t hashSize>
  friend void
  MemoryFingerprintT<Derived, hashSize>::removeFromFingerprintAndDelta(
      MemoryFingerprintDelta &delta);

  template <typename Derived, std::size_t hashSize>
  friend void MemoryFingerprintT<Derived, hashSize>::addToDeltaOnly(
      MemoryFingerprintDelta &delta);

  template <typename Derived, std::size_t hashSize>
  friend void MemoryFingerprintT<Derived, hashSize>::removeFromDeltaOnly(
      MemoryFingerprintDelta &delta);

  template <typename Derived, std::size_t hashSize>
  friend void MemoryFingerprintT<Derived, hashSize>::addDelta(
      MemoryFingerprintDelta &delta);

  template <typename Derived, std::size_t hashSize>
  friend void MemoryFingerprintT<Derived, hashSize>::removeDelta(
      MemoryFingerprintDelta &delta);

  MemoryFingerprint::value_t fingerprintValue{};
  std::unordered_map<const Array *, std::uint64_t> symbolicReferences;
};

} // namespace klee

// NOTE: MemoryFingerprintDelta needs to be a complete type
#define INCLUDE_MEMORYFINGERPRINT_BASE_H
// include definitions of MemoryFingerprintT member methods
#include "MemoryFingerprint_Base.h"
#undef INCLUDE_MEMORYFINGERPRINT_BASE_H

#endif
