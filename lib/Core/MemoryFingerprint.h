#ifndef KLEE_MEMORYFINGERPRINT_H
#define KLEE_MEMORYFINGERPRINT_H

#include "klee/Config/config.h"
#include "klee/Expr.h"

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
class KFunction;
class KInstruction;

class MemoryFingerprintDelta;
class MemoryFingerprint_CryptoPP_BLAKE2b;
template <typename hashT> class VerifiedMemoryFingerprint;

#ifdef ENABLE_VERIFIED_FINGERPRINTS
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
  void addDelta(MemoryFingerprintDelta &delta);
  void removeDelta(MemoryFingerprintDelta &delta);

  valueT getFingerprint(std::vector<ref<Expr>> &expressions);

  valueT getFingerprintWithDelta(std::vector<ref<Expr>> &expressions,
                                 MemoryFingerprintDelta &delta);

  void updateExpr(const ref<Expr> expr);

  void updateConstantExpr(const ConstantExpr &expr);

  template <typename T>
  static std::string toString(const T &fingerprintValue) {
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

  bool updateWriteFragment(std::uint64_t address, ref<Expr> value);
  bool updateLocalFragment(std::uint64_t threadID,
                           std::uint64_t stackFrameIndex,
                           const llvm::Instruction *inst, ref<Expr> value);
  bool updateArgumentFragment(std::uint64_t threadID, std::uint64_t sfIndex,
                              const KFunction *kf, std::uint64_t argumentIndex,
                              ref<Expr> value);
  bool updateProgramCounterFragment(std::uint64_t threadID,
                                    std::uint64_t sfIndex,
                                    const llvm::Instruction *i);
  bool updateFunctionFragment(std::uint64_t threadID, std::uint64_t sfIndex,
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

class MemoryFingerprint_CryptoPP_BLAKE2b
    : public MemoryFingerprintT<MemoryFingerprint_CryptoPP_BLAKE2b, 32> {
  friend class MemoryFingerprintT<MemoryFingerprint_CryptoPP_BLAKE2b, 32>;
  using Base = MemoryFingerprintT<MemoryFingerprint_CryptoPP_BLAKE2b, 32>;
  template <typename hashT> friend class VerifiedMemoryFingerprint;
  template <typename hashT> friend class VerifiedMemoryFingerprintOstream;

  CryptoPP::BLAKE2b blake2b{false, 32};
  MemoryFingerprintOstream<CryptoPP::BLAKE2b> ostream{blake2b};

  void generateHash();
  void clearHash();
  void updateUint8(const std::uint8_t value);
  void updateUint64(const std::uint64_t value);
  llvm::raw_ostream &updateOstream();

public:
  MemoryFingerprint_CryptoPP_BLAKE2b() = default;
  MemoryFingerprint_CryptoPP_BLAKE2b(const MemoryFingerprint_CryptoPP_BLAKE2b &other) : Base(other) { }
  MemoryFingerprint_CryptoPP_BLAKE2b(MemoryFingerprint_CryptoPP_BLAKE2b &&) = delete;
  MemoryFingerprint_CryptoPP_BLAKE2b& operator=(MemoryFingerprint_CryptoPP_BLAKE2b &&) = delete;
  ~MemoryFingerprint_CryptoPP_BLAKE2b() = default;
};

class MemoryFingerprint_StringSet
    : public MemoryFingerprintT<MemoryFingerprint_StringSet, 0, std::set<std::string>> {
  friend class MemoryFingerprintT<MemoryFingerprint_StringSet, 0, std::set<std::string>>;
  using Base = MemoryFingerprintT<MemoryFingerprint_StringSet, 0, std::set<std::string>>;
  template <typename hashT> friend class VerifiedMemoryFingerprint;
  template <typename hashT> friend class VerifiedMemoryFingerprintOstream;

  std::string current;
  bool first = true;
  llvm::raw_string_ostream ostream{current};

  void generateHash();
  void clearHash();
  void updateUint8(const std::uint8_t value);
  void updateUint64(const std::uint64_t value);
  llvm::raw_ostream &updateOstream();

  static void executeAdd(value_t &dst, const value_t &src);
  static void executeRemove(value_t &dst, const value_t &src);

  static std::string toString_impl(const value_t &fingerprintValue);

public:
  MemoryFingerprint_StringSet() = default;
  MemoryFingerprint_StringSet(const MemoryFingerprint_StringSet &other) : Base(other) { }
  MemoryFingerprint_StringSet(MemoryFingerprint_StringSet &&) = delete;
  MemoryFingerprint_StringSet& operator=(MemoryFingerprint_StringSet &&) = delete;
  ~MemoryFingerprint_StringSet() = default;

  struct DecodedFragment {
    std::size_t writes = 0;
    bool containsSymbolicValue = false;
    bool hasPathConstraint = false;
    bool output = false;
  };
  static DecodedFragment decodeAndPrintFragment(llvm::raw_ostream &os,
                                                std::string fragment,
                                                bool showMemoryOperations);
};

template <typename hashT>
class VerifiedMemoryFingerprintValue {
  friend class VerifiedMemoryFingerprint<hashT>;
  friend struct std::hash<VerifiedMemoryFingerprintValue<hashT>>;

  MemoryFingerprint_StringSet::value_t stringSet;
  typename hashT::value_t hash;

public:
  bool operator<(const VerifiedMemoryFingerprintValue<hashT> &other) const {
    return std::tie(hash, stringSet) < std::tie(other.hash, other.stringSet);
  }

  bool operator==(const VerifiedMemoryFingerprintValue<hashT> &other) const {
    if (hash == other.hash)
      assert(stringSet == other.stringSet);
    return hash == other.hash && stringSet == other.stringSet;
  }

  VerifiedMemoryFingerprintValue() = default;
  VerifiedMemoryFingerprintValue(const VerifiedMemoryFingerprintValue &other) = default;
  VerifiedMemoryFingerprintValue(VerifiedMemoryFingerprintValue &&) = delete;
  VerifiedMemoryFingerprintValue& operator=(VerifiedMemoryFingerprintValue &&) = delete;
  ~VerifiedMemoryFingerprintValue() = default;
};

template <typename hashT>
class VerifiedMemoryFingerprintOstream : public llvm::raw_ostream {
private:
  MemoryFingerprint_StringSet &stringSetFingerprint;
  hashT &hashFingerprint;
  std::uint64_t pos = 0;

public:
  explicit VerifiedMemoryFingerprintOstream(MemoryFingerprint_StringSet &s, hashT &h)
    : stringSetFingerprint(s), hashFingerprint(h) { }
  uint64_t current_pos() const override { return pos; }
  ~VerifiedMemoryFingerprintOstream() override { assert(GetNumBytesInBuffer() == 0); }

  void write_impl(const char *ptr, std::size_t size) override {
    std::string str(ptr, size);

    llvm::raw_ostream &stringSetOS = stringSetFingerprint.updateOstream();
    stringSetOS << str;
    stringSetOS.flush();

    llvm::raw_ostream &hashOS = hashFingerprint.updateOstream();
    hashOS << str;
    hashOS.flush();

    pos += size;
  }
};

template <typename hashT>
class VerifiedMemoryFingerprint
    : public MemoryFingerprintT<VerifiedMemoryFingerprint<hashT>, 0, VerifiedMemoryFingerprintValue<hashT>> {
  friend class MemoryFingerprintT<VerifiedMemoryFingerprint<hashT>, 0, VerifiedMemoryFingerprintValue<hashT>>;
  using Base = MemoryFingerprintT<VerifiedMemoryFingerprint<hashT>, 0, VerifiedMemoryFingerprintValue<hashT>>;

  MemoryFingerprint_StringSet stringSetFingerprint;
  hashT hashFingerprint;
  VerifiedMemoryFingerprintOstream<hashT> ostream{stringSetFingerprint, hashFingerprint};

  void generateHash() {
    stringSetFingerprint.generateHash();
    Base::buffer.stringSet = stringSetFingerprint.buffer;
    hashFingerprint.generateHash();
    Base::buffer.hash = hashFingerprint.buffer;
  }

  void clearHash() {
    stringSetFingerprint.clearHash();
    hashFingerprint.clearHash();
  }

  void updateUint8(const std::uint8_t value) {
    stringSetFingerprint.updateUint8(value);
    hashFingerprint.updateUint8(value);
  }

  void updateUint64(const std::uint64_t value) {
    stringSetFingerprint.updateUint64(value);
    hashFingerprint.updateUint64(value);
  }

  llvm::raw_ostream &updateOstream() {
    return ostream;
  }

  static std::string toString_impl(const typename Base::value_t &fingerprintValue) {
    return hashT::toString(fingerprintValue.hash);
  }

  static void executeAdd(typename Base::value_t &dst, const typename Base::value_t &src) {
    MemoryFingerprint_StringSet::executeAdd(dst.stringSet, src.stringSet);
    hashT::executeAdd(dst.hash, src.hash);
  }

  static void executeRemove(typename Base::value_t &dst, const typename Base::value_t &src) {
    MemoryFingerprint_StringSet::executeRemove(dst.stringSet, src.stringSet);
    hashT::executeRemove(dst.hash, src.hash);
  }

public:
  VerifiedMemoryFingerprint() = default;
  VerifiedMemoryFingerprint(const VerifiedMemoryFingerprint &other) : Base(other) { }
  VerifiedMemoryFingerprint(VerifiedMemoryFingerprint &&) = delete;
  VerifiedMemoryFingerprint& operator=(VerifiedMemoryFingerprint &&) = delete;
  ~VerifiedMemoryFingerprint() = default;
};

// NOTE: MemoryFingerprint needs to be a complete type
class MemoryFingerprintDelta {
  friend class MemoryState;

  template <typename Derived, std::size_t hashSize, typename valueT>
  friend void MemoryFingerprintT<Derived, hashSize, valueT>::addToFingerprintAndDelta(MemoryFingerprintDelta &delta);

  template <typename Derived, std::size_t hashSize, typename valueT>
  friend void MemoryFingerprintT<Derived, hashSize, valueT>::removeFromFingerprintAndDelta(MemoryFingerprintDelta &delta);

  template <typename Derived, std::size_t hashSize, typename valueT>
  friend void MemoryFingerprintT<Derived, hashSize, valueT>::addToDeltaOnly(MemoryFingerprintDelta &delta);

  template <typename Derived, std::size_t hashSize, typename valueT>
  friend void MemoryFingerprintT<Derived, hashSize, valueT>::removeFromDeltaOnly(MemoryFingerprintDelta &delta);

  template <typename Derived, std::size_t hashSize, typename valueT>
  friend void MemoryFingerprintT<Derived, hashSize, valueT>::addDelta(MemoryFingerprintDelta &delta);

  template <typename Derived, std::size_t hashSize, typename valueT>
  friend void MemoryFingerprintT<Derived, hashSize, valueT>::removeDelta(MemoryFingerprintDelta &delta);

  MemoryFingerprint::value_t fingerprintValue = {};
  std::unordered_map<const Array *, std::uint64_t> symbolicReferences;
};

} // namespace klee

// NOTE: MemoryFingerprintDelta needs to be a complete type
#define INCLUDE_MEMORYFINGERPRINT_BASE_H
// include definitions of MemoryFingerprintT member methods
#include "MemoryFingerprint_Base.h"
#undef INCLUDE_MEMORYFINGERPRINT_BASE_H

#endif
