#include "ObjectAccesses.h"

#include <iterator>

using namespace klee;

class ObjectAccesses::OperationList::Acquisition {
  std::shared_ptr<ObjectAccesses::OperationList>& self;

public:
  Acquisition(std::shared_ptr<ObjectAccesses::OperationList>& self) : self(self) { }

  // Ensures that the resource is owned. Will return true when it is forked.
  bool acquire() {
    if (self.use_count() > 1) {
      self = std::make_shared<OperationList>(*self);
      return true;
    } else {
      return false;
    }
  }

  template<typename F1>
  Acquisition& acquire(F1 on_acquisition) {
    if (self.use_count() > 1) {
      self = std::make_shared<OperationList>(*self);
      on_acquisition(self.get());
    }
    return *this;
  }

  // The Acquisition object behaves as a smart pointer for const accesses.
  // Note: const-ness is *not statically ensured*, otherwise all iterators to the underlying data
  // structures also become const_iterators, which cannot be upcasted after acquisition.
  [[nodiscard]] ObjectAccesses::OperationList* operator->() noexcept {
    return self.get();
  }

  // The Acquisition object uses the `mut` function for mutable accesses to assert ownership.
  [[nodiscard]] ObjectAccesses::OperationList* mut() noexcept {
    assert(self.use_count() == 1);
    return self.get();
  }
};

void ObjectAccesses::OperationList::registerConcreteMemoryOperation(
        Acquisition self, Address incomingBegin, MemoryOperation &&incoming) {
  auto incomingEnd = incomingBegin + incoming.numBytes;
  auto it = self->concrete.lower_bound(incomingBegin);

  // deal with potential conflict to the left
  if (it != self->concrete.begin() && !(it != self->concrete.end() && it->first == incomingBegin)) {
    auto prev = std::prev(it);
    auto const prevBegin = prev->first;
    auto const prevEnd = prevBegin + prev->second.numBytes;
    if (prevEnd > incomingBegin) {
      if (prev->second.isWrite() || isRead(incoming.type)) {
        if (prevEnd >= incomingEnd) {
          return;
        } else {
          incomingBegin = prevEnd;
        }
      } else {
        auto const numBytes = incomingBegin - prevBegin;
        self.acquire([&it, &prev](auto* self) { it = self->concrete.find(it->first); prev = std::prev(it); });
        prev->second.numBytes = numBytes;
        if (prevEnd > incomingEnd) {
          self.mut()->concrete.emplace_hint(it, incomingBegin, std::move(incoming));

          auto acc = prev->second;
          acc.numBytes = prevEnd - incomingEnd;
          self.mut()->concrete.emplace_hint(it, incomingEnd, std::move(acc));

          return;
        }
      }
    }
  }

  assert(it == self->concrete.end() || it->first >= incomingBegin);

  decltype(self->concrete)::node_type node;
  // deal with potential conflicts to the right
  while (it != self->concrete.end() && it->first < incomingEnd) {
    auto itBegin = it->first;
    auto itEnd = itBegin + it->second.numBytes;

    if (itBegin == incomingBegin) {
      if (isRead(incoming.type) || it->second.isWrite()) {
        if (itEnd >= incomingEnd) {
          return;
        } else {
          incomingBegin = itEnd;
          ++it;
        }
      } else {
        self.acquire([&it](auto* self) { it = self->concrete.find(it->first); });
        if (itEnd == incomingEnd) {
          it->second.instruction = incoming.instruction;
          it->second.type = incoming.type;
          return;
        } else if (itEnd < incomingEnd) {
          auto next = std::next(it);
          node = self.mut()->concrete.extract(it);
          it = next;
        } else {
          assert(itEnd > incomingEnd);
          incoming.numBytes = incomingEnd - incomingBegin;
          auto next = std::next(it);
          auto node2 = self.mut()->concrete.extract(it);
          if (node.empty()) {
            self.mut()->concrete.emplace_hint(next, incomingBegin, std::move(incoming));
          } else {
            node.key() = incomingBegin;
            node.mapped() = std::move(incoming);
            self.mut()->concrete.insert(next, std::move(node));
          }
          node2.key() = incomingEnd;
          self.mut()->concrete.insert(next, std::move(node2));
          return;
        }
      }
    } else {
      assert(itBegin > incomingBegin);
      if (isWrite(incoming.type) || it->second.isRead()) {
        self.acquire([&it](auto* self) { it = self->concrete.find(it->first); });
        if (itEnd <= incomingEnd) {
          auto next = std::next(it);
          node = self.mut()->concrete.extract(it);
          it = next;
        } else {
          assert(itEnd > incomingEnd);
          incoming.numBytes = incomingEnd - incomingBegin;
          if (node.empty()) {
            self.mut()->concrete.emplace_hint(it, incomingBegin, std::move(incoming));
          } else {
            node.key() = incomingBegin;
            node.mapped() = std::move(incoming);
            self.mut()->concrete.insert(it, std::move(node));
          }
          auto next = std::next(it);
          node = self.mut()->concrete.extract(it);
          node.key() = incomingEnd;
          self.mut()->concrete.insert(next, std::move(node));
          return;
        }
      } else {
        if (itEnd >= incomingEnd) {
          incoming.numBytes = itBegin - incomingBegin;
          if (node.empty()) {
            self.mut()->concrete.emplace_hint(it, incomingBegin, std::move(incoming));
          } else {
            node.key() = incomingBegin;
            node.mapped() = std::move(incoming);
            self.mut()->concrete.insert(it, std::move(node));
          }
          return;
        } else {
          auto acc = incoming;
          acc.numBytes = itBegin - incomingBegin;
          if (node.empty()) {
            self.mut()->concrete.emplace_hint(it, incomingBegin, std::move(acc));
          } else {
            node.key() = incomingBegin;
            node.mapped() = std::move(acc);
            self.mut()->concrete.insert(it, std::move(node));
          }
          incomingBegin = itEnd;
          ++it;
        }
      }
    }
  }

  incoming.numBytes = incomingEnd - incomingBegin;
  if (it == self->concrete.end()) {
    self.acquire([&it](auto* self) { it = self->concrete.end(); });
  } else {
    self.acquire([&it](auto* self) { it = self->concrete.find(it->first); });
  }
  if (node.empty()) {
    self.mut()->concrete.emplace_hint(it, incomingBegin, std::move(incoming));
  } else {
    node.key() = incomingBegin;
    node.mapped() = std::move(incoming);
    self.mut()->concrete.insert(it, std::move(node));
  }
}

void ObjectAccesses::OperationList::registerSymbolicMemoryOperation(
        Acquisition self, MemoryOperation &&incoming) {
  decltype(self->symbolic)::node_type node;

  auto [it, end] = self->symbolic.equal_range(incoming.offset);
  while (it != end) {
    // assert(it->first == incoming.offset);
    if (it->second.numBytes >= incoming.numBytes && (it->second.isWrite() || isRead(incoming.type))) {
      return;
    }

    if (it->second.numBytes <= incoming.numBytes && (it->second.isRead() || isWrite(incoming.type))) {
      if (self.acquire()) {
        // sadly, we need to restart iteration, as there is no reasonable way to translate the iterators
        std::tie(it, end) = self->symbolic.equal_range(incoming.offset);
      } else {
        auto next = std::next(it);
        node = self.mut()->symbolic.extract(it);
        it = std::move(next);
      }
    } else {
      ++it;
    }
  }

  if (self.acquire()) {
    assert(node.empty());
    self.mut()->symbolic.emplace(std::move(incoming.offset), std::move(incoming));
  } else {
    if (node.empty()) {
      self.mut()->symbolic.emplace_hint(end, std::move(incoming.offset), std::move(incoming));
    } else {
      // assert(node.key() == incoming.offset && "All sources of `node` have the same offset as the incoming operation");
      node.mapped() = std::move(incoming);
      self.mut()->symbolic.insert(end, std::move(node));
    }
  }
}

void ObjectAccesses::OperationList::registerMemoryOperation(
        std::shared_ptr<OperationList>& self, MemoryOperation &&incoming) {
  assert(isRead(incoming.type) || isWrite(incoming.type));

  if(auto incomingOffsetExpr = dyn_cast<ConstantExpr>(incoming.offset)) {
    auto incomingOffset = incomingOffsetExpr->getZExtValue();
    registerConcreteMemoryOperation(self, incomingOffset, std::move(incoming));
  } else {
    registerSymbolicMemoryOperation(self, std::move(incoming));
  }
}

void ObjectAccesses::trackMemoryOperation(MemoryOperation&& mop) {
  assert(mop.type != AccessType::UNKNOWN);

  if (allocFreeInstruction != nullptr) {
    // No other access can be better than the one that we currently track
    return;
  }

  if (klee::isAllocOrFree(mop.type)) {
    // This is the best access to track as it races with every other one
    accesses = nullptr;
    allocFreeInstruction = mop.instruction;
    return;
  }

  if (accesses == nullptr) {
    // So we started with an empty list, make sure that we create one first
    accesses = std::make_shared<OperationList>();
  }

  // So this is one standard r/w access and we track those
  OperationList::registerMemoryOperation(accesses, std::move(mop));
}
