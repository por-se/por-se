// This is an incomplete file, included from MemoryFingerprint.h

#ifndef INCLUDE_FROM_MEMORYFINGERPRINT_H
static_assert(0, "DO NOT include this file directly!");
#endif

namespace klee {
class ExecutionState;

template <typename hashT>
class VerifiedMemoryFingerprintValue {
  friend class VerifiedMemoryFingerprint<hashT>;
  friend struct std::hash<VerifiedMemoryFingerprintValue<hashT>>;

  MemoryFingerprint_StringSet::value_t stringSet;
  typename hashT::value_t hash;

public:
  const ExecutionState *state = nullptr;

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
  VerifiedMemoryFingerprintValue(VerifiedMemoryFingerprintValue &&) = default;
  VerifiedMemoryFingerprintValue& operator=(VerifiedMemoryFingerprintValue &&) = default;
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
    Base::buffer.state = state;

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

  void updateUint16(const std::uint16_t value) {
    stringSetFingerprint.updateUint16(value);
    hashFingerprint.updateUint16(value);
  }

  void updateUint64(const std::uint64_t value) {
    stringSetFingerprint.updateUint64(value);
    hashFingerprint.updateUint64(value);
  }

  llvm::raw_ostream &updateOstream() {
    return ostream;
  }

  static std::string toString_impl(const typename Base::value_t &fingerprintValue) {
    return MemoryFingerprint_StringSet::toString(fingerprintValue.stringSet);
  }

  static void executeAdd(typename Base::value_t &dst, const typename Base::value_t &src) {
    dst.state = src.state;
    bool success = MemoryFingerprint_StringSet::executeAdd(dst.stringSet, src.stringSet);
    if (!success) {
      assert(src.state != nullptr && "state not correctly propagated");
      dst.state->printFingerprint();
      llvm::errs().flush();
      assert(0 && "fragment already in fingerprint");
    }
    hashT::executeAdd(dst.hash, src.hash);
  }

  static void executeRemove(typename Base::value_t &dst, const typename Base::value_t &src) {
    dst.state = src.state;
    bool success = MemoryFingerprint_StringSet::executeRemove(dst.stringSet, src.stringSet);
    if (!success) {
      assert(src.state != nullptr && "state not correctly propagated");
      dst.state->printFingerprint();
      llvm::errs().flush();
      assert(0 && "fragment not in fingerprint");
    }
    hashT::executeRemove(dst.hash, src.hash);
  }

public:
  const ExecutionState *state;

  VerifiedMemoryFingerprint() = default;
  VerifiedMemoryFingerprint(const VerifiedMemoryFingerprint &other) : Base(other), state(other.state) { }
  VerifiedMemoryFingerprint(VerifiedMemoryFingerprint &&) = delete;
  VerifiedMemoryFingerprint& operator=(VerifiedMemoryFingerprint &&) = delete;
  ~VerifiedMemoryFingerprint() = default;
};

} // namespace klee
