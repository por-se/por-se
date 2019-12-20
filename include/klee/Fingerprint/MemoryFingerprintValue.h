#ifndef KLEE_MEMORYFINGERPRINTVALUE_H
#define KLEE_MEMORYFINGERPRINTVALUE_H

#ifndef KLEE_OUTSIDE_BUILD_TREE
// for ENABLE_VERIFIED_FINGERPRINTS
#include "klee/Config/config.h"
#endif

#include <array>
#include <cstdint>
#include <cassert>
#include <map>
#include <tuple>

namespace klee {

#ifndef ENABLE_VERIFIED_FINGERPRINTS

using MemoryFingerprintValue = std::array<std::uint8_t, 32>;

#else
class MemoryFingerprint_StringSet;
class MemoryFingerprint_CryptoPP_BLAKE2b;
template <typename hashT> class VerifiedMemoryFingerprint;

using MemoryFingerprintValue_StringSet = std::map<std::string, std::int64_t>;
using MemoryFingerprintValue_CryptoPP_BLAKE2b = std::array<std::uint8_t, 32>;

template <typename hashT>
class VerifiedMemoryFingerprintValue {
  friend class VerifiedMemoryFingerprint<hashT>;
  friend struct std::hash<VerifiedMemoryFingerprintValue<hashT>>;

  MemoryFingerprintValue_StringSet stringSet;
  MemoryFingerprintValue_CryptoPP_BLAKE2b hash;

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
  VerifiedMemoryFingerprintValue(const VerifiedMemoryFingerprintValue &) = default;
  VerifiedMemoryFingerprintValue(VerifiedMemoryFingerprintValue &&) = default;
  VerifiedMemoryFingerprintValue& operator=(const VerifiedMemoryFingerprintValue &) = default;
  VerifiedMemoryFingerprintValue& operator=(VerifiedMemoryFingerprintValue &&) = default;
  ~VerifiedMemoryFingerprintValue() = default;
};

using MemoryFingerprintValue = VerifiedMemoryFingerprintValue<MemoryFingerprint_CryptoPP_BLAKE2b>;

#endif

} // namespace klee

#endif // KLEE_MEMORYFINGERPRINTVALUE_H
