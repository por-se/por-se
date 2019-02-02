#include "MemoryFingerprint.h"

#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/KModule.h"

#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/CommandLine.h"

namespace {
llvm::cl::opt<bool>ShowMemoryOperations("verified-fingerprints-show-memory",
  llvm::cl::init(false),
  llvm::cl::desc("Show individual (per byte) memory operations in verified fingerprints (default=on)")
);
} // namespace

namespace klee {

void MemoryFingerprint_StringSet::updateUint8(const std::uint8_t value) {
  if (first) {
    first = false;
  } else {
    current += " ";
  }
  current += std::to_string(static_cast<unsigned>(value));
}

void MemoryFingerprint_StringSet::updateUint64(const std::uint64_t value) {
  if (first) {
    first = false;
  } else {
    current += " ";
  }
  current += std::to_string(value);
}

llvm::raw_ostream &MemoryFingerprint_StringSet::updateOstream() {
  if (first) {
    first = false;
  } else {
    current += " ";
  }
  return ostream;
}

void MemoryFingerprint_StringSet::generateHash() { buffer.insert(current); }

void MemoryFingerprint_StringSet::clearHash() {
  current = "";
  buffer.clear();
  first = true;
}

void MemoryFingerprint_StringSet::executeAdd(value_t &dst, const value_t &src) {
  for (auto &elem : src) {
    if (elem.empty())
      continue;

    if (dst.find(elem) != dst.end()) {
      std::string debugStr;
      llvm::raw_string_ostream debug(debugStr);
      llvm::errs() << "FAILED: \n";
      decodeAndPrintFragment(debug, elem, true);
      llvm::errs() << debug.str() << "\n";
      llvm::errs() << "DST: " << toString(dst) << "\n";
      llvm::errs().flush();
      assert(0 && "fragment already in fingerprint");
    }
    dst.insert(elem);
  }
}

void MemoryFingerprint_StringSet::executeRemove(value_t &dst, const value_t &src) {
  for (auto &elem : src) {
    if (elem.empty())
      continue;

    auto pos = dst.find(elem);
    if (pos == dst.end()) {
      std::string debugStr;
      llvm::raw_string_ostream debug(debugStr);
      llvm::errs() << "FAILED: \n";
      decodeAndPrintFragment(debug, elem, true);
      llvm::errs() << debug.str() << "\n";
      llvm::errs() << "DST: " << toString(dst) << "\n";
      llvm::errs().flush();
      assert(0 && "fragment not in fingerprint");
    }
    dst.erase(pos);
  }
}

MemoryFingerprint_StringSet::DecodedFragment
MemoryFingerprint_StringSet::decodeAndPrintFragment(llvm::raw_ostream &os,
                                                    std::string fragment,
                                                    bool showMemoryOperations) {
  DecodedFragment result;

    std::istringstream item(fragment);
    int id;
    item >> id;
    switch (id) {
    case 2:
      result.containsSymbolicValue = true;
      // fallthrough
    case 1:
      if (showMemoryOperations) {
        std::uint64_t addr;
        item >> addr;

        os << "[G]Write: ";
        os << addr;
        os << " =";

        if (id == 2) {
          std::string value;
          for (std::string line; std::getline(item, line);) {
            os << line;
          }
        } else {
          unsigned value;
          item >> value;
          os << " " << value;
        }
        result.output = true;
      }
      result.writes++;
      break;
    case 4:
      result.containsSymbolicValue = true;
      // fallthrough
    case 3: {
      std::uint64_t tid;
      std::uint64_t sfid;
      std::uintptr_t ptr;

      item >> tid;
      item >> sfid;
      item >> ptr;
      llvm::Instruction *inst = reinterpret_cast<llvm::Instruction *>(ptr);

      os << "[T" << tid << ':' << sfid << ']';
      os << "Local: %";
      if (inst->hasName()) {
        os << inst->getName();
      } else {
        // extract slot number
        std::string line;
        llvm::raw_string_ostream sos(line);
        sos << *inst;
        sos.flush();
        std::size_t start = line.find("%") + 1;
        std::size_t end = line.find(" ", start);
        os << line.substr(start, end - start);
      }

      const llvm::DebugLoc &dl = inst->getDebugLoc();
      if (dl) {
        auto *scope = cast_or_null<llvm::DIScope>(dl.getScope());
        if (scope) {
          os << " (" << scope->getFilename();
          os << ":" << dl.getLine();
          os << ")";
        }
      }
      os << " =";

      for (std::string line; std::getline(item, line);) {
        os << line;
      }
      result.output = true;
      break;
    }
    case 6:
      result.containsSymbolicValue = true;
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

      os << "[T" << tid << ':' << sfid << ']';
      os << "Argument: ";
      os << kf->function->getName() << "(";
      for (std::size_t i = 0; i < total; ++i) {
        if (argumentIndex == i) {
          for (std::string line; std::getline(item, line);) {
            os << line;
          }
        } else {
          os << "?";
        }
        if (i != total - 1) {
          os << ", ";
        }
      }
      os << ")";
      result.output = true;
      break;
    }
    case 7: {
      std::uint64_t tid;
      std::uint64_t sfid;
      std::uintptr_t ptr;

      item >> tid;
      item >> sfid;
      item >> ptr;
      llvm::Instruction *i = reinterpret_cast<llvm::Instruction *>(ptr);

      os << "[T" << tid << ':' << sfid << ']';
      os << "Program Counter: ";
      os << i;
      os << " in ";
      os << i->getFunction()->getName();

      result.output = true;
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

      os << "[T" << tid << ':' << sfid << ']';
      os << "Stack Frame: ";
      os << callee->function->getName();
      os << " (called from ";
      os << caller->inst;
      os << ")";

      result.output = true;
      break;
    }
    case 9: {
      std::size_t externalCallNum;

      item >> externalCallNum;

      os << "[G]External Function Call: ";
      os << externalCallNum;

      result.output = true;
      break;
    }
    case 10: {
      os << "[G]Path Constraint:";

      for (std::string line; std::getline(item, line);) {
        os << line;
        result.hasPathConstraint = true;
      }
      result.output = true;
      break;
    }
    default:
      os << "[UNKNOWN:";
      os << fragment;
      os << "]";
      result.output = true;
    }

  return result;
}

std::string MemoryFingerprint_StringSet::toString_impl(const value_t &fingerprintValue) {
  std::string result_str;
  llvm::raw_string_ostream result(result_str);
  std::size_t writes = 0;
  bool containsSymbolicValue = false;
  bool hasPathConstraint = false;

  result << "{";

  for (auto it = fingerprintValue.begin(); it != fingerprintValue.end(); ++it) {
    auto res = decodeAndPrintFragment(result, *it, ShowMemoryOperations);
    writes += res.writes;

    if (res.containsSymbolicValue)
      containsSymbolicValue = true;

    if (res.hasPathConstraint)
      hasPathConstraint = true;

    if (std::next(it) != fingerprintValue.end() && res.output) {
      result << ", ";
    }
  }

  if (!ShowMemoryOperations) {
    result << "} + " << writes << " write(s)";
  } else {
    result << "}";
  }

  assert(!hasPathConstraint || (hasPathConstraint && containsSymbolicValue));

  return result.str();
}

} // namespace klee
