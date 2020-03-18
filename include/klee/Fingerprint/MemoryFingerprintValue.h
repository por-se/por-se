#ifndef KLEE_MEMORYFINGERPRINTVALUE_H
#define KLEE_MEMORYFINGERPRINTVALUE_H

// for ENABLE_VERIFIED_FINGERPRINTS
#include "klee/Config/config.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cassert>
#include <iterator>
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
  bool isDiff = false;

public:
  bool operator<(const VerifiedMemoryFingerprintValue<hashT> &other) const {
    return std::tie(hash, stringSet) < std::tie(other.hash, other.stringSet);
  }

  bool operator==(const VerifiedMemoryFingerprintValue<hashT> &other) const {
    if (hash == other.hash)
      assert(stringSet == other.stringSet);
    return hash == other.hash && stringSet == other.stringSet;
  }

  VerifiedMemoryFingerprintValue<hashT> diff(const VerifiedMemoryFingerprintValue<hashT> &other) const {
    VerifiedMemoryFingerprintValue<hashT> difference;
    difference.hash = {};
    difference.isDiff = true;

    std::set_symmetric_difference(stringSet.begin(), stringSet.end(),
                                  other.stringSet.begin(), other.stringSet.end(),
                                  std::inserter(difference.stringSet, difference.stringSet.end()));

    return difference;
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
