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
  std::unordered_map<const Array *, std::uint64_t> symbolicReferences;
};

} // namespace klee

#endif // KLEE_MEMORYFINGERPRINTDELTA_H
