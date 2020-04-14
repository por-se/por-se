#include "ObjectAccesses.h"

#include <iterator>

using namespace klee;

class ObjectAccesses::OperationList::Acquisition {
  ObjectAccesses::OperationList* self;
  const void* from;
  std::shared_ptr<ObjectAccesses::OperationList> fork;

public:
  Acquisition(ObjectAccesses::OperationList* self, const void* from)
    : self(self), from(from)
  { }

  // Ensures that the resource is owned. Will return true when it is forked.
  bool acquire() {
    if (self->owner != from) {
      fork = std::make_shared<OperationList>(*self);
      fork->owner = from;
      self = fork.get();
      return true;
    } else {
      return false;
    }
  }

  template<typename F1>
  Acquisition& acquire(F1 on_acquisition) {
    if (self->owner != from) {
      fork = std::make_shared<OperationList>(*self);
      fork->owner = from;
      self = fork.get();
      on_acquisition(self);
    }
    return *this;
  }

  // The Acquisition object behaves as a smart pointer for const accesses.
  // Note: const-ness is *not statically ensured*, otherwise all iterators to the underlying data
  // structures also become const_iterators, which cannot be upcasted after acquisition.
  [[nodiscard]] ObjectAccesses::OperationList* operator->() noexcept {
    return self;
  }

  // The Acquisition object uses the `mut` function for mutable accesses to assert ownership.
  [[nodiscard]] ObjectAccesses::OperationList* mut() noexcept {
    assert(self->owner == from);
    return self;
  }

  [[nodiscard]] std::shared_ptr<ObjectAccesses::OperationList> result() {
    return std::move(fork);
  }
};

void ObjectAccesses::OperationList::registerConcreteMemoryOperation(
        Acquisition& self, AccessMetaData::Offset const incomingOffset, MemoryOperation &&incoming) {
  if (self->concrete.empty()) {
    self.acquire();
    self.mut()->concrete.emplace(incomingOffset, std::move(incoming));
    return;
  }

  auto incomingBegin = incomingOffset;
  auto incomingEnd = incomingBegin + incoming.numBytes;
  decltype(self->concrete)::node_type node;
  auto it = self->concrete.lower_bound(incomingBegin);

  if (it != self->concrete.end() && it->first == incomingBegin) {
    // fastpath: exact offset match
    auto itBegin = it->first;
    auto itEnd = itBegin + it->second.numBytes;
    if (it->second.isWrite() || incoming.isRead()) {
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
        it = std::move(next);
      } else {
        assert(itEnd > incomingEnd);
        auto next = std::next(it);
        node = self.mut()->concrete.extract(it);
        self.mut()->concrete.emplace_hint(next, incomingBegin, std::move(incoming));
        node.key() = incomingEnd;
        node.mapped().offset = Expr::createPointer(incomingEnd);
        self.mut()->concrete.insert(next, std::move(node));
        return;
      }
    }
  } else if (it != self->concrete.begin()) {
    // deal with potential conflict to the left
    auto prev = std::prev(it);
    if (prev->first + prev->second.numBytes > incomingBegin) {
      if (prev->second.isWrite() || incoming.isRead()) {
        incomingBegin = prev->first + prev->second.numBytes;
      } else {
        self.acquire([&it, &prev](auto* self) { it = self->concrete.find(it->first); prev = std::prev(it); });
        prev->second.numBytes = incomingBegin - prev->first;
      }
    }
  }

  assert(it == self->concrete.end() || it->first >= incomingBegin);

  // deal with potential conflicts to the right
  while (it != self->concrete.end() && it->first < incomingEnd) {
    auto itBegin = it->first;
    auto itEnd = itBegin + it->second.numBytes;

    if (itBegin == incomingBegin) {
      if (incoming.isRead() || it->second.isWrite()) {
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
          if (incomingOffset != incomingBegin) {
            incoming.offset = Expr::createPointer(incomingBegin);
            incoming.numBytes = incomingEnd - incomingBegin;
          }
          auto next = std::next(it);
          auto node2 = self.mut()->concrete.extract(it);
          if (node.empty()) {
            self.mut()->concrete.emplace_hint(next, incomingBegin, std::move(incoming));
          } else {
            node.key() = incomingBegin;
            node.mapped() = std::move(incoming);
            self.mut()->concrete.insert(next, std::move(node));
          }
          node.key() = incomingEnd;
          node.mapped().offset = Expr::createPointer(incomingEnd);
          self.mut()->concrete.insert(next, std::move(node));
          return;
        }
      }
    } else {
      assert(itBegin > incomingBegin);
      if (incoming.isWrite() || it->second.isRead()) {
        self.acquire([&it](auto* self) { it = self->concrete.find(it->first); });
        if (itEnd <= incomingEnd) {
          auto next = std::next(it);
          node = self.mut()->concrete.extract(it);
          it = next;
        } else {
          assert(itEnd > incomingEnd);
          if (incomingOffset != incomingBegin) {
            incoming.offset = Expr::createPointer(incomingBegin);
            incoming.numBytes = incomingEnd - incomingBegin;
          }
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
          node.mapped().offset = Expr::createPointer(incomingEnd);
          self.mut()->concrete.insert(next, std::move(node));
          return;
        }
      } else {
        if (itEnd >= incomingEnd) {
          if (incomingOffset != incomingBegin) {
            incoming.offset = Expr::createPointer(incomingBegin);
          }
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
          if (incomingOffset != incomingBegin) {
            acc.offset = Expr::createPointer(incomingBegin);
          }
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

  if (incomingOffset != incomingBegin) {
    incoming.offset = Expr::createPointer(incomingBegin);
    incoming.numBytes = incomingEnd - incomingBegin;
  }
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
        Acquisition& self, MemoryOperation &&incoming) {
  decltype(self->symbolic)::node_type node;
  auto [it, end] = self->symbolic.equal_range(incoming.offset);
  while (it != end) {
    assert(it->second.offset == incoming.offset && it->first == incoming.offset);
    if (it->second.numBytes >= incoming.numBytes && (it->second.isWrite() || incoming.isRead())) {
      return;
    }

    if (it->second.numBytes <= incoming.numBytes && (it->second.isRead() || incoming.isWrite())) {
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
    self.mut()->symbolic.emplace(incoming.offset, std::move(incoming));
  } else {
    if (node.empty()) {
      auto offset = incoming.offset;
      self.mut()->symbolic.emplace_hint(end, std::move(offset), std::move(incoming));
    } else {
      node.key() = incoming.offset;
      node.mapped() = std::move(incoming);
      self.mut()->symbolic.insert(end, std::move(node));
    }
  }
}

std::shared_ptr<ObjectAccesses::OperationList> ObjectAccesses::OperationList::registerMemoryOperation(
        MemoryOperation &&incoming, const void *from) {
  assert(incoming.isRead() || incoming.isWrite());

  Acquisition self(this, from);
  if(auto incomingOffsetExpr = dyn_cast<ConstantExpr>(incoming.offset)) {
    auto incomingOffset = incomingOffsetExpr->getZExtValue();
    registerConcreteMemoryOperation(self, incomingOffset, std::move(incoming));
  } else {
    registerSymbolicMemoryOperation(self, std::move(incoming));
  }
  return self.result();
}

void ObjectAccesses::trackMemoryOperation(MemoryOperation&& mop) {
  assert(mop.type != AccessMetaData::Type::UNKNOWN);

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
  auto fork = accesses->registerMemoryOperation(std::move(mop), this);
  if (fork) {
    accesses = fork;
  }
}
