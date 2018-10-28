#ifndef KLEE_MEMORYFINGERPRINT_H
#define KLEE_MEMORYFINGERPRINT_H

#include "klee/Expr.h"
#include "klee/Config/config.h"

// stub for typeid to use CryptoPP without RTTI
template<typename T> const std::type_info& FakeTypeID(void) {
    assert(0 && "CryptoPP tries to use typeid()");
}
#define typeid(a) FakeTypeID<a>()
#include <cryptopp/sha.h>
#include <cryptopp/blake2.h>
#undef typeid

#include <array>
#include <iomanip>
#include <string>
#include <type_traits>
#include <unordered_set>

namespace klee {

class MemoryFingerprint_CryptoPP_SHA1;
class MemoryFingerprint_CryptoPP_BLAKE2b;
class MemoryFingerprint_Dummy;

// Set default implementation
using MemoryFingerprint = MemoryFingerprint_CryptoPP_BLAKE2b;

template<typename Derived, size_t hashSize>
class MemoryFingerprintT {

protected:
  using hash_t = std::array<std::uint8_t, hashSize>;
  using dummy_t = std::set<std::string>;

public:
  typedef typename std::conditional<hashSize == 0, dummy_t, hash_t>::type
    fingerprint_t;

private:
  Derived& getDerived() {
    return *(static_cast<Derived*>(this));
  }

  fingerprint_t fingerprint = {};
  fingerprint_t fingerprintStackFrameDelta = {};

  template<typename T,
    typename std::enable_if<std::is_same<T, hash_t>::value, int>::type = 0>
  inline void executeXOR(T &dst, const T &src) {
    for (std::size_t i = 0; i < hashSize; ++i) {
      dst[i] ^= src[i];
    }
  }

  template<typename T,
    typename std::enable_if<std::is_same<T, dummy_t>::value, int>::type = 0>
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
  fingerprint_t buffer = {};


public:
  MemoryFingerprintT() = default;
  MemoryFingerprintT(const MemoryFingerprintT &) = default;

  void applyToFingerprint() {
    getDerived().generateHash();
    executeXOR(fingerprint, buffer);
    getDerived().clearHash();
  }

  void applyToFingerprintStackFrameDelta() {
    applyToFingerprintStackFrameDelta(fingerprintStackFrameDelta);
  }

  void applyToFingerprintStackFrameDelta(fingerprint_t &deltaDst) {
    getDerived().generateHash();
    executeXOR(deltaDst, buffer);
    // All changes that are applied to stack frame deltas are also applied to
    // fingerprint immediately, since we need to be able to remove just the
    // changes of a single stack frame in a simple manner.
    executeXOR(fingerprint, buffer);
    getDerived().clearHash();
  }

  fingerprint_t getFingerprint() {
    return fingerprint;
  }

  fingerprint_t getStackFrameDelta() {
    return fingerprintStackFrameDelta;
  }

  void applyAndResetStackFrameDelta() {
    // This is trivial as changes made in fingerprintStackFrameDelta are already
    // applied to fingerprint. Thus, we just need to do the reset part.
    fingerprintStackFrameDelta = {};
  }

  // WARNING: This function can only be used with a stackFrameDelta that has
  // been created and modified using the current instance of MemoryFingerprint,
  // as it assumes that every change recorded in stackFrameDelta has also been
  // applied to fingerprint.
  void setStackFrameDeltaToPreviousValue(fingerprint_t stackFrameDelta) {
    // TODO: write separate dummy method that actually checks this assumption?
    fingerprintStackFrameDelta = stackFrameDelta;
  }

  void discardStackFrameDelta() {
    executeXOR(fingerprint, fingerprintStackFrameDelta);
    fingerprintStackFrameDelta = {};
  }

  void discardEverything() {
    fingerprintStackFrameDelta = {};
    fingerprint = {};
  }

  void updateConstantExpr(const ConstantExpr &expr) {
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

  template<typename T,
    typename std::enable_if<std::is_same<T, hash_t>::value, int>::type = 0>
  static std::string toString(const T &fingerprint) {
    std::stringstream result;
    for (auto iter = fingerprint.cbegin(); iter != fingerprint.cend(); ++iter) {
      result << std::hex << std::setfill('0') << std::setw(2);
      result << static_cast<unsigned int>(*iter);
    }
    return result.str();
  }

  template<typename T,
    typename std::enable_if<std::is_same<T, dummy_t>::value, int>::type = 0>
  static std::string toString(const T &fingerprint) {
    return Derived::toString_impl(fingerprint);
  }

  std::string getFingerprintAsString() {
    return toString(getFingerprint());
  }

  std::string getStackFrameDeltaAsString() {
    return toString(fingerprintStackFrameDelta);
  }
};


class MemoryFingerprint_CryptoPP_SHA1 :
public MemoryFingerprintT<MemoryFingerprint_CryptoPP_SHA1, CryptoPP::SHA1::DIGESTSIZE> {
friend class MemoryFingerprintT<MemoryFingerprint_CryptoPP_SHA1, CryptoPP::SHA1::DIGESTSIZE>;
private:
  CryptoPP::SHA1 sha1;
  void generateHash();
  void clearHash();

public:
  void updateUint8(const std::uint8_t value);
  void updateUint64(const std::uint64_t value);
  void updateExpr(ref<Expr> expr);
};


class MemoryFingerprint_CryptoPP_BLAKE2b :
public MemoryFingerprintT<MemoryFingerprint_CryptoPP_BLAKE2b, 32> {
friend class MemoryFingerprintT<MemoryFingerprint_CryptoPP_BLAKE2b, 32>;
private:
  CryptoPP::BLAKE2b blake2b = CryptoPP::BLAKE2b(false, 32);
  void generateHash();
  void clearHash();

public:
  void updateUint8(const std::uint8_t value);
  void updateUint64(const std::uint64_t value);
  void updateExpr(ref<Expr> expr);
};


template <typename T>
class MemoryFingerprint_ostream : public llvm::raw_ostream {
private:
  T &hash;
  std::uint64_t pos = 0;

public:
  explicit MemoryFingerprint_ostream(T &_hash) : hash(_hash) {}
  void write_impl(const char *ptr, std::size_t size) override;
  uint64_t current_pos() const override { return pos; }
  ~MemoryFingerprint_ostream() override { flush(); }
};


class MemoryFingerprint_Dummy :
public MemoryFingerprintT<MemoryFingerprint_Dummy, 0> {
friend class MemoryFingerprintT<MemoryFingerprint_Dummy, 0>;
private:
  std::string current;
  bool first = true;
  void generateHash();
  void clearHash();
  static std::string toString_impl(MemoryFingerprintT::dummy_t fingerprint);

public:
  void updateUint8(const std::uint8_t value);
  void updateUint64(const std::uint64_t value);
  void updateExpr(ref<Expr> expr);
};


}
