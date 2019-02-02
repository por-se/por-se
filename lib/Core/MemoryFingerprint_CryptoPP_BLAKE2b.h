// This is an incomplete file, included from MemoryFingerprint.h

#ifndef INCLUDE_FROM_MEMORYFINGERPRINT_H
static_assert(0, "DO NOT include this file directly!");
#endif

namespace klee {

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

} // namespace klee
