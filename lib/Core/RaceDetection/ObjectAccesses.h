#pragma once

#include "CommonTypes.h"

#include <memory>
#include <vector>

namespace klee {
  /// Tracks the least amount of accesses to a single memory object without loosing data
  /// for the data race detection.
  class ObjectAccesses {
    private:
      struct OperationList {
        std::vector<AccessMetaData> list;

        const void* owner = nullptr;

        OperationList() = default;
        OperationList(const OperationList& o) = default;

        // Both methods receive a `from` parameter that indicates which @class{ObjectAccesses}
        // is accessing the list. If we have to make changes to the list and the owner and the
        // from field are not matching, then we have to make a copy of the list.
        // -> In that case we return a shared ptr to the copy/fork of the list with the updated data

        std::shared_ptr<OperationList> registerMemoryOperation(const MemoryOperation& incoming, const void* from);

        std::shared_ptr<OperationList> replace(const MemoryOperation& op, const void* from, std::size_t at);
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
      ObjectAccesses() = default;
      ObjectAccesses(const ObjectAccesses& al) = default;

      [[nodiscard]] bool isAllocOrFree() const {
        return allocFreeInstruction != nullptr;
      };

      [[nodiscard]] KInstruction* getAllocFreeInstruction() const {
        assert(allocFreeInstruction != nullptr);
        return allocFreeInstruction;
      };

      [[nodiscard]] const std::vector<AccessMetaData>& getAccesses() const {
        assert(allocFreeInstruction == nullptr);
        return accesses->list;
      }

      void trackMemoryOperation(const MemoryOperation& mop);
  };
}