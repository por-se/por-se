#pragma once

#include "klee/ThreadId.h"
#include "klee/Internal/Module/KInstruction.h"
#include "../Memory.h"

namespace klee {
  // This files contains basic structures used throughout the data race detection

  class SolverInterface {
    public:
      [[nodiscard]] virtual std::optional<bool> mustBeTrue(ref<Expr> expr) const = 0;
      [[nodiscard]] virtual std::optional<bool> mustBeFalse(ref<Expr> expr) const = 0;
      [[nodiscard]] virtual std::optional<bool> mayBeTrue(ref<Expr> expr) const = 0;
      [[nodiscard]] virtual std::optional<bool> mayBeFalse(ref<Expr> expr) const = 0;
  };

  struct AccessMetaData {
    enum class Type : std::uint8_t {
      UNKNOWN = 0,

      WRITE,
      READ,

      ATOMIC_WRITE,
      ATOMIC_READ,

      ALLOC,
      FREE
    };

    KInstruction* instruction = nullptr;

    Type type = Type::UNKNOWN;

    ref<Expr> offset;

    std::size_t numBytes = 0;

    AccessMetaData() = default;
    AccessMetaData(const AccessMetaData& o) = default;

    [[nodiscard]] bool isConstantOffset() const {
      return offset->getKind() == ConstantExpr::kind;
    }

    [[nodiscard]] bool isExtendedBy(const AccessMetaData& data) const {
      assert(!isAlloc() && !isFree() && !data.isAlloc() && !data.isFree());

      return offset == data.offset && numBytes <= data.numBytes;
    };

    [[nodiscard]] bool isEmbeddedIn(const AccessMetaData& data) const {
      assert(!isAlloc() && !isFree() && !data.isAlloc() && !data.isFree());

      if (offset == data.offset) {
        return numBytes <= data.numBytes;
      }

      if (!isConstantOffset() || !data.isConstantOffset()) {
        return false;
      }

      auto boundStart1 = dyn_cast<ConstantExpr>(offset.get())->getZExtValue();
      auto boundStart2 = dyn_cast<ConstantExpr>(data.offset.get())->getZExtValue();

      auto boundEnd1 = boundStart1 + numBytes;
      auto boundEnd2 = boundStart2 + data.numBytes;

      return boundStart1 >= boundStart2 && boundEnd1 <= boundEnd2;
    };

    [[nodiscard]] std::optional<bool> isOverlappingWith(const AccessMetaData& data) const {
      assert(!isAlloc() && !isFree() && !data.isAlloc() && !data.isFree());

      if (offset == data.offset) {
        return true;
      }

      if (!isConstantOffset() || !data.isConstantOffset()) {
        return {};
      }

      auto boundStart1 = dyn_cast<ConstantExpr>(offset.get())->getZExtValue();
      auto boundStart2 = dyn_cast<ConstantExpr>(data.offset.get())->getZExtValue();

      // Both overlap if one is not completely placed before the other
      // Note that we cannot overflow since these bytes have to be accessed and we currently
      // do not support pointer addresses with more than 64 bits
      auto boundEnd1 = boundStart1 + numBytes;
      auto boundEnd2 = boundStart2 + data.numBytes;

      return !(boundStart1 > boundEnd2 || boundStart2 > boundEnd1);
    };

    [[nodiscard]] ref<Expr> getOffsetOfLastAccessedByte() const {
      assert(!isAlloc() && !isFree());

      // We have to subtract 1 from the num of bytes since we want to check the accessed bytes
      // numBytes == 1 -> only the byte at `offset` was accessed, therefore the last accessed one is:
      //   offset + (numBytes - 1) = offset
      return AddExpr::create(offset, ConstantExpr::create(numBytes - 1, 64));
    };

    [[nodiscard]] bool isWrite() const {
      return type == Type::WRITE || type == Type::ATOMIC_WRITE;
    }

    [[nodiscard]] bool isRead() const {
      return type == Type::READ || type == Type::ATOMIC_READ;
    }

    [[nodiscard]] bool isAtomic() const {
      return type == Type::ATOMIC_WRITE || type == Type::ATOMIC_READ;
    }

    [[nodiscard]] bool isAlloc() const {
      return type == Type::ALLOC;
    }

    [[nodiscard]] bool isFree() const {
      return type == Type::FREE;
    }

    [[nodiscard]] std::string getTypeString() const {
      switch (type) {
        case Type::UNKNOWN: return "un";
        case Type::WRITE: return "w";
        case Type::READ: return "r";

        case Type::ATOMIC_WRITE: return "a+w";
        case Type::ATOMIC_READ: return "a+r";

        case Type::ALLOC: return "a";
        case Type::FREE: return "f";
      }

      return "-";
    }
  };

  struct MemoryOperation : public AccessMetaData {
    // Operation by whom
    ThreadId tid;

    // On what memory object
    const MemoryObject* object;
  };

  struct RaceDetectionResult {
    bool isRace = false;

    KInstruction* racingInstruction = nullptr;
    ThreadId racingThread;

    /// Whether the race can also be a safe access (depending on symbolic choices)
    bool canBeSafe = false;
    /// The condition under which the access is safe
    ref<Expr> conditionToBeSafe;

    // In case of no race, these here are valid
    bool hasNewConstraints = false;
    ref<Expr> newConstraints;
  };
}