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

  enum class AccessType : std::uint8_t {
    UNKNOWN = 0,

    WRITE,
    READ,

    ALLOC,
    FREE
  };

  [[nodiscard]] inline bool isWrite(AccessType type) noexcept {
    return type == AccessType::WRITE;
  }

  [[nodiscard]] inline bool isRead(AccessType type) noexcept {
    return type == AccessType::READ;
  }

  [[nodiscard]] inline bool isAlloc(AccessType type) noexcept {
    return type == AccessType::ALLOC;
  }

  [[nodiscard]] inline bool isFree(AccessType type) noexcept {
    return type == AccessType::FREE;
  }

  [[nodiscard]] inline bool isAllocOrFree(AccessType type) noexcept {
    return type == AccessType::ALLOC || type == AccessType::FREE;
  }

  template<typename S>
  S& operator<<(S& out, AccessType type) {
    if (type == AccessType::UNKNOWN) {
      out << "un";
    } else if (type == AccessType::WRITE) {
      out << "w";
    } else if (type == AccessType::READ) {
      out << "r";
    } else if (type == AccessType::ALLOC) {
      out << "a";
    } else if (type == AccessType::FREE) {
      out << "f";
    } else {
      out << "-";
    }
    return out;
  }

  struct MemoryOperation {
    using Offset = std::size_t;

    KInstruction* instruction = nullptr;

    AccessType type = AccessType::UNKNOWN;

    // Operation by whom
    ThreadId tid;

    // On what memory object
    const MemoryObject* object;

    ref<Expr> offset;
    Offset numBytes = 0;
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
