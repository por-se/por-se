#include "MemoryFingerprint.h"

#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/KModule.h"

#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/LLVMContext.h"

namespace klee {

/* MemoryFingerprintOstream<CryptoPP::BLAKE2b> */

template <>
void MemoryFingerprintOstream<CryptoPP::BLAKE2b>::write_impl(
    const char *ptr, std::size_t size) {
  hash.Update(reinterpret_cast<const CryptoPP::byte *>(ptr), size);
  pos += size;
}

/* MemoryFingerprint_CryptoPP_BLAKE2b */

void MemoryFingerprint_CryptoPP_BLAKE2b::updateUint8(const std::uint8_t value) {
  static_assert(sizeof(CryptoPP::byte) == sizeof(std::uint8_t));
  blake2b.Update(&value, 1);
}

void MemoryFingerprint_CryptoPP_BLAKE2b::updateUint64(
    const std::uint64_t value) {
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

/* MemoryFingerprint_Dummy */

void MemoryFingerprint_Dummy::updateUint8(const std::uint8_t value) {
  if (first) {
    first = false;
  } else {
    current += " ";
  }
  current += std::to_string(static_cast<unsigned>(value));
}

void MemoryFingerprint_Dummy::updateUint64(const std::uint64_t value) {
  if (first) {
    first = false;
  } else {
    current += " ";
  }
  current += std::to_string(value);
}

llvm::raw_ostream &MemoryFingerprint_Dummy::updateOstream() {
  if (first) {
    first = false;
  } else {
    current += " ";
  }
  return ostream;
}

void MemoryFingerprint_Dummy::generateHash() { buffer.insert(current); }

void MemoryFingerprint_Dummy::clearHash() {
  current = "";
  buffer.clear();
  first = true;
}

std::string MemoryFingerprint_Dummy::toString_impl(dummy_t fingerprintValue) {
  std::string result_str;
  llvm::raw_string_ostream result(result_str);
  std::size_t writes = 0;
  bool containsSymbolicValue = false;
  bool hasPathConstraint = false;

  // show individual memory operations in detail: writes (per byte)
  bool showMemoryOperations = false;

  result << "{";

  for (auto it = fingerprintValue.begin(); it != fingerprintValue.end(); ++it) {
    std::istringstream item(*it);
    int id;
    item >> id;
    bool output = false;
    switch (id) {
    case 2:
      containsSymbolicValue = true;
      // fallthrough
    case 1:
      if (showMemoryOperations) {
        std::uint64_t addr;
        item >> addr;

        result << "[G]Write: ";
        result << addr;
        result << " =";

        if (id == 2) {
          std::string value;
          for (std::string line; std::getline(item, line);) {
            result << line;
          }
        } else {
          unsigned value;
          item >> value;
          result << " " << value;
        }
        output = true;
      }
      writes++;
      break;
    case 4:
      containsSymbolicValue = true;
      // fallthrough
    case 3: {
      std::uint64_t tid;
      std::uint64_t sfid;
      std::uintptr_t ptr;

      item >> tid;
      item >> sfid;
      item >> ptr;
      llvm::Instruction *inst = reinterpret_cast<llvm::Instruction *>(ptr);

      result << "[T" << tid << ':' << sfid << ']';
      result << "Local: %";
      if (inst->hasName()) {
        result << inst->getName();
      } else {
        // extract slot number
        std::string line;
        llvm::raw_string_ostream sos(line);
        sos << *inst;
        sos.flush();
        std::size_t start = line.find("%") + 1;
        std::size_t end = line.find(" ", start);
        result << line.substr(start, end - start);
      }

      const llvm::DebugLoc &dl = inst->getDebugLoc();
      if (dl) {
        auto *scope = cast_or_null<llvm::DIScope>(dl.getScope());
        if (scope) {
          result << " (" << scope->getFilename();
          result << ":" << dl.getLine();
          result << ")";
        }
      }
      result << " =";

      for (std::string line; std::getline(item, line);) {
        result << line;
      }
      output = true;
      break;
    }
    case 6:
      containsSymbolicValue = true;
      // fallthrough
    case 5: {
      std::uint64_t tid;
      std::uint64_t sfid;
      std::uintptr_t ptr;
      std::size_t argumentIndex;

      item >> tid;
      item >> sfid;
      item >> ptr;
      KFunction *kf = reinterpret_cast<KFunction *>(ptr);
      item >> argumentIndex;
      std::size_t total = kf->function->arg_size();

      result << "[T" << tid << ':' << sfid << ']';
      result << "Argument: ";
      result << kf->function->getName() << "(";
      for (std::size_t i = 0; i < total; ++i) {
        if (argumentIndex == i) {
          for (std::string line; std::getline(item, line);) {
            result << line;
          }
        } else {
          result << "?";
        }
        if (i != total - 1) {
          result << ", ";
        }
      }
      result << ")";
      output = true;
      break;
    }
    case 7: {
      std::uint64_t tid;
      std::uint64_t sfid;
      std::uintptr_t ptr;

      item >> tid;
      item >> sfid;
      item >> ptr;
      llvm::BasicBlock *bb = reinterpret_cast<llvm::BasicBlock *>(ptr);

      result << "[T" << tid << ':' << sfid << ']';
      result << "Program Counter: ";
      result << bb->getName();
      result << " in ";
      result << bb->getParent()->getName();

      output = true;
      break;
    }
    case 8: {
      std::uint64_t tid;
      std::uint64_t sfid;
      std::uintptr_t callerPtr;
      std::uintptr_t calleePtr;

      item >> tid;
      item >> sfid;
      item >> callerPtr;
      item >> calleePtr;

      KInstruction *caller = reinterpret_cast<KInstruction *>(callerPtr);
      KFunction *callee = reinterpret_cast<KFunction *>(calleePtr);

      result << "[T" << tid << ':' << sfid << ']';
      result << "Stack Frame: ";
      result << callee->function->getName();
      result << " (called from ";
      result << caller->inst;
      result << ")";

      output = true;
      break;
    }
    case 9: {
      std::size_t externalCallNum;

      item >> externalCallNum;

      result << "[G]External Function Call: ";
      result << externalCallNum;

      output = true;
      break;
    }
    case 10: {
      result << "[G]Path Constraint:";

      for (std::string line; std::getline(item, line);) {
        result << line;
        hasPathConstraint = true;
      }
      output = true;
      break;
    }
    default:
      result << "[UNKNOWN:";
      result << *it;
      result << "]";
      output = true;
    }
    if (std::next(it) != fingerprintValue.end() && output) {
      result << ", ";
    }
  }

  if (!showMemoryOperations) {
    result << "} + " << writes << " write(s)";
  } else {
    result << "}";
  }

  assert(!hasPathConstraint || (hasPathConstraint && containsSymbolicValue));

  return result.str();
}

} // namespace klee
