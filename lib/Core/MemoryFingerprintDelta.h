// This is an incomplete file, included from MemoryFingerprint.h

#ifndef INCLUDE_FROM_MEMORYFINGERPRINT_H
static_assert(0, "DO NOT include this file directly!");
#endif

namespace klee {

class MemoryFingerprintDelta {
  friend class MemoryState;

  template <typename Derived, std::size_t hashSize, typename valueT>
  friend std::string MemoryFingerprintT<Derived, hashSize, valueT>::toString(const MemoryFingerprintDelta &delta);

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
