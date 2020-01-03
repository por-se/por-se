#ifndef KLEE_MEMORYFINGERPRINTDELTA_H
#define KLEE_MEMORYFINGERPRINTDELTA_H

#include "klee/Fingerprint/MemoryFingerprintValue.h"

#include <cstdint>
#include <unordered_map>

namespace klee {
class Array;

class MemoryFingerprintDelta {
  friend class MemoryState;

  template <typename Derived, std::size_t hashSize, typename valueT>
  friend class MemoryFingerprintT;

  MemoryFingerprintValue fingerprintValue = {};
  std::unordered_map<const Array *, std::int64_t> symbolicReferences;

  friend bool operator==(const MemoryFingerprintDelta &lhs, const MemoryFingerprintDelta &rhs) {
    return lhs.fingerprintValue == rhs.fingerprintValue && lhs.symbolicReferences == rhs.symbolicReferences;
  }

  friend bool operator!=(const MemoryFingerprintDelta &lhs, const MemoryFingerprintDelta &rhs) {
    return !(lhs == rhs);
  }
};

} // namespace klee

#endif // KLEE_MEMORYFINGERPRINTDELTA_H
