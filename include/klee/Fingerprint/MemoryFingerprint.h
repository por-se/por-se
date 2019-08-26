#ifndef KLEE_MEMORYFINGERPRINT_H
#define KLEE_MEMORYFINGERPRINT_H

#include "klee/Config/config.h"
#include "klee/Expr/Expr.h"
#include "klee/Expr/ExprHashMap.h"
#include "klee/Expr/ExprPPrinter.h"

#include "klee/ThreadId.h"

#include "llvm/Support/raw_ostream.h"

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
#include <cstddef>
#include <iomanip>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace llvm {
class Instruction;
} // namespace llvm

namespace klee {
struct KFunction;
struct KInstruction;

class MemoryFingerprintDelta;
class MemoryFingerprint_CryptoPP_BLAKE2b;

#ifdef ENABLE_VERIFIED_FINGERPRINTS
template <typename hashT> class VerifiedMemoryFingerprint;
using MemoryFingerprint = VerifiedMemoryFingerprint<MemoryFingerprint_CryptoPP_BLAKE2b>;
#else
using MemoryFingerprint = MemoryFingerprint_CryptoPP_BLAKE2b;
#endif

template <typename Derived, std::size_t hashSize, typename valueT = std::array<std::uint8_t, hashSize>>
class MemoryFingerprintT {
  template <typename hashT> friend class VerifiedMemoryFingerprint;
  Derived &getDerived() { return *(static_cast<Derived *>(this)); }

  valueT fingerprintValue = {};
  std::unordered_map<const Array *, std::uint64_t> symbolicReferences;

  template <typename T>
  static void executeXOR(T &dst, const T &src) {
    for (std::size_t i = 0; i < hashSize; ++i) {
      dst[i] ^= src[i];
    }
  }

  template <typename T>
  static void executeAdd(T &dst, const T &src) {
    if constexpr (hashSize > 0) {
      executeXOR(dst, src);
    } else {
      Derived::executeAdd(dst, src);
    }
  }

  template <typename T>
  static void executeRemove(T &dst, const T &src) {
    if constexpr (hashSize > 0) {
      executeXOR(dst, src);
    } else {
      Derived::executeRemove(dst, src);
    }
  }

protected:
  // buffer that holds current hash after calling generateHash()
  valueT buffer = {};

private:
  // information on what went into buffer
  bool bufferContainsSymbolic = false;
  std::unordered_map<const Array *, std::uint64_t> bufferSymbolicReferences;

  void resetBufferRefCount() {
    bufferContainsSymbolic = false;
    bufferSymbolicReferences.clear();
  }

  void updateThreadId(const ThreadId& tid);

public:
  MemoryFingerprintT() = default;
  MemoryFingerprintT(const MemoryFingerprintT &) = default;

  using value_t = valueT;

  void addToFingerprint();
  void removeFromFingerprint();

  void addToFingerprintAndDelta(MemoryFingerprintDelta &delta);
  void removeFromFingerprintAndDelta(MemoryFingerprintDelta &delta);
  void addToDeltaOnly(MemoryFingerprintDelta &delta);
  void removeFromDeltaOnly(MemoryFingerprintDelta &delta);
  void addDelta(const MemoryFingerprintDelta &delta);
  void removeDelta(const MemoryFingerprintDelta &delta);

  valueT getFingerprint(std::vector<ref<Expr>> &expressions);

  valueT getFingerprintWithDelta(std::vector<ref<Expr>> &expressions,
                                 const MemoryFingerprintDelta &delta);

  void updateExpr(const ref<Expr> &expr);

  void updateConstantExpr(const ConstantExpr &expr);

  static std::string toString(const valueT &fingerprintValue) {
    if constexpr (hashSize > 0) {
      std::stringstream result;
      for (const auto byte : fingerprintValue) {
        result << std::hex << std::setfill('0') << std::setw(2);
        result << static_cast<unsigned int>(byte);
      }
      return result.str();
    } else {
      return Derived::toString_impl(fingerprintValue);
    }
  }

  static std::string toString(const MemoryFingerprintDelta &delta);

  bool updateWriteFragment(std::uint64_t address, ref<Expr> value);
  bool updateLocalFragment(const ThreadId &threadID,
                           std::uint64_t stackFrameIndex,
                           const llvm::Instruction *inst, ref<Expr> value);
  bool updateArgumentFragment(const ThreadId &threadID, std::uint64_t sfIndex,
                              const KFunction *kf, std::uint64_t argumentIndex,
                              ref<Expr> value);
  bool updateProgramCounterFragment(const ThreadId &threadID,
                                    std::uint64_t sfIndex,
                                    const llvm::Instruction *i);
  bool updateFunctionFragment(const ThreadId &threadID, std::uint64_t sfIndex,
                              const KFunction *callee,
                              const KInstruction *caller);
  bool updateExternalCallFragment(std::uint64_t externalCallCounter);
};

template <typename T>
class MemoryFingerprintOstream : public llvm::raw_ostream {
  T &hash;
  std::uint64_t pos = 0;

public:
  explicit MemoryFingerprintOstream(T &_hash) : hash(_hash) {}
  void write_impl(const char *ptr, std::size_t size) override;
  uint64_t current_pos() const override { return pos; }
  ~MemoryFingerprintOstream() override { assert(GetNumBytesInBuffer() == 0); }
};

} // namespace klee

#define INCLUDE_FROM_MEMORYFINGERPRINT_H

#include "bits/MemoryFingerprint_CryptoPP_BLAKE2b.h"
#ifdef ENABLE_VERIFIED_FINGERPRINTS
#include "bits/MemoryFingerprint_StringSet.h"
#include "bits/MemoryFingerprint_Verified.h"
#endif // ENABLE_VERIFIED_FINGERPRINTS

// NOTE: MemoryFingerprint needs to be a complete type
#include "bits/MemoryFingerprintDelta.h"

// NOTE: MemoryFingerprintDelta needs to be a complete type
#include "bits/MemoryFingerprint_Base.h"

#undef INCLUDE_FROM_MEMORYFINGERPRINT_H

namespace klee {
  using MemoryFingerprintValue = MemoryFingerprint::value_t;
}

#endif
