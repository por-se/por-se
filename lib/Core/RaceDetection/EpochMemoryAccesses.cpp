#include "EpochMemoryAccesses.h"

using namespace klee;

void EpochMemoryAccesses::pruneDataForMemoryObject(const MemoryObject* obj) {
  memoryOperations.erase(obj->address);
}

void EpochMemoryAccesses::trackMemoryOperation(MemoryOperation&& op) {
  auto& accesses = memoryOperations[op.object->address];
  accesses.trackMemoryOperation(std::move(op));
}

const ObjectAccesses* EpochMemoryAccesses::getMemoryAccessesOfThread(const MemoryObject* mo) const {
  if(auto it = memoryOperations.find(mo->address); it != memoryOperations.end()) {
    return &it->second;
  } else {
    return nullptr;
  }
}
