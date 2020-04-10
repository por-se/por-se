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
      virtual ~SolverInterface() = default;
  };

  struct AccessMetaData {
    static const std::size_t NO_CONST_OFFSET = static_cast<std::size_t>(-1);

    enum class OverlapResult : std::uint8_t {
      UNKNOWN = 0,
      OVERLAP = 1,
      NO_OVERLAP = 2
    };

    enum class Type : std::uint8_t {
      UNKNOWN = 0,

      WRITE,
      READ,

      ALLOC,
      FREE
    };

    KInstruction* instruction = nullptr;

    Type type = Type::UNKNOWN;

    ref<Expr> offset;
    std::size_t offsetConst = NO_CONST_OFFSET;

    std::size_t numBytes = 0;

    AccessMetaData() = default;
    AccessMetaData(const AccessMetaData& o) = default;

    [[nodiscard]] bool isConstantOffset() const {
      return offsetConst != NO_CONST_OFFSET;
    }

    /// Only returns `true` if `*this` is a subrange of `data` that starts at the same offset
    /// Note: This is an incomplete check that may return `false` even if the condition is met.
    [[nodiscard]] bool isExtendedBy(const AccessMetaData& data) const {
      assert(!isAlloc() && !isFree() && !data.isAlloc() && !data.isFree());

      return numBytes <= data.numBytes && ((isConstantOffset() && offsetConst == data.offsetConst) || offset == data.offset);
    };

    /// Only returns `true` if `*this` is a subrange of `data`
    /// Note: This is an incomplete check that may return `false` even if the condition is met.
    [[nodiscard]] bool isEmbeddedIn(const AccessMetaData& data) const {
      assert(!isAlloc() && !isFree() && !data.isAlloc() && !data.isFree());

      if (!isConstantOffset() && !data.isConstantOffset()) {
        return offset == data.offset && numBytes <= data.numBytes;
      }

      if (!isConstantOffset() || !data.isConstantOffset()) {
        return false;
      }

      auto boundEnd1 = offsetConst + numBytes;
      auto boundEnd2 = data.offsetConst + data.numBytes;

      return offsetConst >= data.offsetConst && boundEnd1 <= boundEnd2;
    };

    /// Returns `OverlapResult::OVERLAP` if `*this` shares at least one byte with `data`.
    /// Returns `Overlap::NO_OVERLAP` if `*this` shares at least one byte with `data`.
    /// If the check cannot be determined (cheaply), `OverlapResult::UNKNOWN` is returned.
    [[nodiscard]] OverlapResult isOverlappingWith(const AccessMetaData& data) const {
      assert(!isAlloc() && !isFree() && !data.isAlloc() && !data.isFree());

      if (isConstantOffset()) {
        if (offsetConst == data.offsetConst) {
          assert(numBytes > 0);
          return OverlapResult::OVERLAP;
        }
        if (!data.isConstantOffset()) {
          return OverlapResult::UNKNOWN;
        }

        // Both overlap if one is not completely placed before the other
        // Note that we cannot overflow since these bytes have to be accessed and we currently
        // do not support pointer addresses with more than 64 bits
        auto boundEnd1 = offsetConst + numBytes;
        auto boundEnd2 = data.offsetConst + data.numBytes;

        return offsetConst < boundEnd2 && data.offsetConst < boundEnd1 ? OverlapResult::OVERLAP : OverlapResult::NO_OVERLAP;
      } else {
        if (data.isConstantOffset()) {
          return OverlapResult::UNKNOWN;
        }

        if (offset == data.offset) {
          assert(numBytes > 0);
          return OverlapResult::OVERLAP;
        } else {
          return OverlapResult::UNKNOWN;
        }
      }
    };

    [[nodiscard]] ref<Expr> getOffsetOfLastAccessedByte() const {
      assert(!isAlloc() && !isFree());

      // We have to subtract 1 from the num of bytes since we want to check the accessed bytes
      // numBytes == 1 -> only the byte at `offset` was accessed, therefore the last accessed one is:
      //   offset + (numBytes - 1) = offset
      return AddExpr::create(offset, ConstantExpr::create(numBytes - 1, 64));
    };

    [[nodiscard]] bool isWrite() const {
      return type == Type::WRITE;
    }

    [[nodiscard]] bool isRead() const {
      return type == Type::READ;
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
