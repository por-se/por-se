#include "EpochMemoryAccesses.h"

using namespace klee;

void EpochMemoryAccesses::pruneDataForMemoryObject(const MemoryObject* obj) {
  memoryOperations.erase(obj->getId());
}

void EpochMemoryAccesses::trackMemoryOperation(const MemoryOperation& op) {
  auto moId = op.object->getId();
  auto it = memoryOperations.find(moId);

  if (it == memoryOperations.end()) {
    auto insertId = memoryOperations.insert(std::make_pair(moId, ObjectAccesses{}));
    assert(insertId.second);
    insertId.first->second.trackMemoryOperation(op);
    return;
  }

  it->second.trackMemoryOperation(op);
}

std::optional<std::reference_wrapper<const ObjectAccesses>> EpochMemoryAccesses::getMemoryAccessesOfThread(
        const MemoryObject* mo) const {
  auto moId = mo->getId();
  auto it = memoryOperations.find(moId);

  if (it == memoryOperations.end()) {
    return {};
  }

  return it->second;
}