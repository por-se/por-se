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

    using Offset = std::size_t;

    KInstruction* instruction = nullptr;

    Type type = Type::UNKNOWN;

    Offset numBytes = 0;

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

    ref<Expr> offset;
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
