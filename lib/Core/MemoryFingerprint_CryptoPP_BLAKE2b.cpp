#include "MemoryFingerprint.h"

namespace klee {

template <>
void MemoryFingerprintOstream<CryptoPP::BLAKE2b>::write_impl(const char *ptr, std::size_t size) {
  hash.Update(reinterpret_cast<const CryptoPP::byte *>(ptr), size);
  pos += size;
}

void MemoryFingerprint_CryptoPP_BLAKE2b::updateUint8(const std::uint8_t value) {
  static_assert(sizeof(CryptoPP::byte) == sizeof(std::uint8_t));
  blake2b.Update(&value, 1);
}

void MemoryFingerprint_CryptoPP_BLAKE2b::updateUint16(const std::uint16_t value) {
  static_assert(sizeof(CryptoPP::byte) == sizeof(std::uint8_t));
  blake2b.Update(reinterpret_cast<const std::uint8_t *>(&value), 2);
}

void MemoryFingerprint_CryptoPP_BLAKE2b::updateUint64(const std::uint64_t value) {
  static_assert(sizeof(CryptoPP::byte) == sizeof(std::uint8_t));
  blake2b.Update(reinterpret_cast<const std::uint8_t *>(&value), 8);
}

llvm::raw_ostream &MemoryFingerprint_CryptoPP_BLAKE2b::updateOstream() {
  return ostream;
}

void MemoryFingerprint_CryptoPP_BLAKE2b::generateHash() {
  blake2b.Final(buffer.data());
}

void MemoryFingerprint_CryptoPP_BLAKE2b::clearHash() {
  // not really necessary as Final() already calls this internally
  blake2b.Restart();
}

} // namespace klee
