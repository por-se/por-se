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
    private:
      struct OperationList {
        class Acquisition;

        std::map<AccessMetaData::Offset, AccessMetaData> concrete;
        std::multimap<ref<Expr>, AccessMetaData> symbolic;

        static void registerMemoryOperation(std::shared_ptr<OperationList>& self, MemoryOperation&& incoming);

      private:
        static void registerConcreteMemoryOperation(Acquisition self, AccessMetaData::Offset const incomingOffset, MemoryOperation&& incoming);
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

      [[nodiscard]] const AccessMetaData& getAnyAccess() const noexcept {
        assert(allocFreeInstruction == nullptr);
        assert(!accesses->concrete.empty() || !accesses->symbolic.empty());
        return accesses->concrete.empty() ? accesses->symbolic.begin()->second : accesses->concrete.begin()->second;
      }


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
