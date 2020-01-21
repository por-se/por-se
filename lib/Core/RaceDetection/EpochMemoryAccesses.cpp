#include "EpochMemoryAccesses.h"

using namespace klee;

void EpochMemoryAccesses::pruneDataForMemoryObject(const MemoryObject* obj) {
  memoryOperations.erase(obj->address);
}

void EpochMemoryAccesses::trackMemoryOperation(const MemoryOperation& op) {
  memoryOperations[op.object->address].trackMemoryOperation(op);
}

const ObjectAccesses* EpochMemoryAccesses::getMemoryAccessesOfThread(const MemoryObject* mo) const {
  if(auto it = memoryOperations.find(mo->address); it != memoryOperations.end()) {
    return &it->second;
  } else {
    return nullptr;
  }
}
