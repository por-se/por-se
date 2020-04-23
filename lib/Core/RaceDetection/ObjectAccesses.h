#pragma once

#include "CommonTypes.h"

#include <map>
#include <memory>
#include <tuple>
#include <vector>

namespace klee {
  /// Tracks the least amount of accesses to a single memory object without loosing data
  /// for the data race detection.
  class ObjectAccesses {
    public:
      using Address = std::size_t;
      using Offset = MemoryOperation::Offset;

      // key: Address begin
      struct ConcreteAccess {
        Offset numBytes;
        AccessType type;
        KInstruction* instruction;

        ConcreteAccess(MemoryOperation const& op)
          : numBytes(op.numBytes)
          , type(op.type)
          , instruction(op.instruction)
        {
          assert(klee::isRead(type) || klee::isWrite(type));
        }

        ConcreteAccess& operator=(MemoryOperation const& op) {
          assert(klee::isRead(op.type) || klee::isWrite(op.type));

          numBytes = op.numBytes;
          type = op.type;
          instruction = op.instruction;

          return *this;
        }

        [[nodiscard]] bool isRead() const noexcept { return klee::isRead(type); }
        [[nodiscard]] bool isWrite() const noexcept { return klee::isWrite(type); }
      };

      // key: ref<Expr> base
      struct SymbolicAccess {
        Offset numBytes;
        AccessType type;
        KInstruction* instruction;

        SymbolicAccess(MemoryOperation const& op)
          : numBytes(op.numBytes)
          , type(op.type)
          , instruction(op.instruction)
        {
          assert(klee::isRead(type) || klee::isWrite(type));
        }

        SymbolicAccess& operator=(MemoryOperation const& op) {
          assert(klee::isRead(op.type) || klee::isWrite(op.type));

          numBytes = op.numBytes;
          type = op.type;
          instruction = op.instruction;
          
          return *this;
        }


        [[nodiscard]] bool isRead() const noexcept { return klee::isRead(type); }
        [[nodiscard]] bool isWrite() const noexcept { return klee::isWrite(type); }
      };

    private:
      struct OperationList {
        class Acquisition;

        std::map<Address, ConcreteAccess> concrete;
        std::multimap<ref<Expr>, SymbolicAccess> symbolic;

        static void registerMemoryOperation(std::shared_ptr<OperationList>& self, MemoryOperation&& incoming);

      private:
        static void registerConcreteMemoryOperation(Acquisition self, Address incomingOffset, MemoryOperation&& incoming);
        static void registerSymbolicMemoryOperation(Acquisition self, MemoryOperation&& incoming);
      };

      // Small optimization: we can save the whole overhead of maintaining the list and
      // tracking all access meta data in the case, that we have an alloc or free:
      // -> no other access races with more types, bounds, etc
      // -> offset, numBytes etc are always not needed
      // -> only the actual instruction is interesting for debug purposes
      KInstruction* allocFreeInstruction = nullptr;

      // null if @see{allocFreeInstruction} is set (e.g. free or alloc)
      std::shared_ptr<OperationList> accesses;

    public:
      [[nodiscard]] bool isAllocOrFree() const noexcept {
        return allocFreeInstruction != nullptr;
      };

      [[nodiscard]] KInstruction* getAllocFreeInstruction() const noexcept {
        assert(allocFreeInstruction != nullptr);
        return allocFreeInstruction;
      };

      [[nodiscard]] const auto& getConcreteAccesses() const noexcept {
        assert(allocFreeInstruction == nullptr);
        return accesses->concrete;
      }

      [[nodiscard]] const auto& getSymbolicAccesses() const noexcept {
        assert(allocFreeInstruction == nullptr);
        return accesses->symbolic;
      }

      void trackMemoryOperation(MemoryOperation&& mop);
  };
}
