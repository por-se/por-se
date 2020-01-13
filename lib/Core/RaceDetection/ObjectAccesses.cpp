#include "ObjectAccesses.h"

using namespace klee;

std::shared_ptr<ObjectAccesses::OperationList> ObjectAccesses::OperationList::replace(
        const MemoryOperation& op, const void* from, std::size_t at) {
  assert(at < list.size());

  if (from == owner) {
    auto& data = list[at];

    data.type = op.type;
    data.instruction = op.instruction;

    return nullptr;
  } else {
    // So we are not the owner, ergo make a copy
    auto fork = std::make_shared<OperationList>(*this);
    fork->owner = from;
    auto res = fork->replace(op, from, at);
    assert(res == nullptr);

    // We do not have to clear the current owner of this list (e.g. to prevent changes)
    // since all contents were copied. Further modifications do not touch the same
    // objects

    return fork;
  }
}

std::shared_ptr<ObjectAccesses::OperationList> ObjectAccesses::OperationList::registerMemoryOperation(
        const MemoryOperation &incoming, const void *from) {

  assert(!incoming.isAlloc() && !incoming.isFree());
  assert(incoming.type != MemoryOperation::Type::UNKNOWN);

  // Test if the current access is mergeable with an older one
  for (std::size_t i = 0; i < list.size(); i++) {
    const auto& opData = list[i];

    if (opData.type == incoming.type && opData.isEmbeddedIn(incoming)) {
      return replace(incoming, from, i);
    }

    if (!opData.isExtendedBy(incoming)) {
      continue;
    }

    if (opData.isWrite()) {
      // This race type races with every other type -> tracking the incoming one does not add value
      return nullptr;
    }

    // We know now for sure that the saved on is a read
    if (incoming.isWrite()) {
      // So the incoming one is racing with every type
      return replace(incoming, from, i);
    }

    return nullptr;
  }

  // So the deduplication was not successful. Simply add it
  AccessMetaData data(incoming); // NOLINT(cppcoreguidelines-slicing)

  if (from == owner) {
    list.emplace_back(data);
    return nullptr;
  } else {
    // So we are not the owner, ergo make a copy
    auto fork = std::make_shared<OperationList>(*this);
    fork->owner = from;
    fork->list.emplace_back(data);
    return fork;
  }
}

void ObjectAccesses::trackMemoryOperation(const MemoryOperation& mop) {
  if (allocFreeInstruction != nullptr) {
    // No other access can be better than the one that we currently track
    return;
  }

  if (mop.isAlloc() || mop.isFree()) {
    // This is the best access to track as it races with every other one
    accesses = nullptr;
    allocFreeInstruction = mop.instruction;
    return;
  }

  if (accesses == nullptr) {
    // So we started with an empty list, make sure that we create one first
    accesses = std::make_shared<OperationList>();
    accesses->owner = this;
  }

  // So this is one standard r/w access and we track those
  auto fork = accesses->registerMemoryOperation(mop, this);
  if (fork) {
    accesses = fork;
  }
}
