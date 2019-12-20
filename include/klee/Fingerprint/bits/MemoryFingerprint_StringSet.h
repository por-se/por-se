// This is an incomplete file, included from MemoryFingerprint.h

#ifndef INCLUDE_FROM_MEMORYFINGERPRINT_H
static_assert(0, "DO NOT include this file directly!");
#endif

namespace klee {

class MemoryFingerprint_StringSet
    : public MemoryFingerprintT<MemoryFingerprint_StringSet, 0, std::map<std::string, std::int64_t>> {
  friend class MemoryFingerprintT<MemoryFingerprint_StringSet, 0, std::map<std::string, std::int64_t>>;
  using Base = MemoryFingerprintT<MemoryFingerprint_StringSet, 0, std::map<std::string, std::int64_t>>;
  template <typename hashT> friend class VerifiedMemoryFingerprint;
  template <typename hashT> friend class VerifiedMemoryFingerprintOstream;

  std::string current;
  bool first = true;
  llvm::raw_string_ostream ostream{current};

  void generateHash();
  void clearHash();
  void updateUint8(const std::uint8_t value);
  void updateUint16(const std::uint16_t value);
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

  static std::string decodeTid(std::istringstream &stream);
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

} // namespace klee
