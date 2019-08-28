//===-- MemoryManager.h -----------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_MEMORYMANAGER_H
#define KLEE_MEMORYMANAGER_H

#include "klee/ThreadId.h"
#include "klee/Thread.h"

#include "pseudoalloc.h"

#include <cstddef>
#include <set>
#include <map>

namespace llvm {
class Value;
}

namespace klee {
class MemoryObject;
class ArrayCache;

class MemoryManager {
public:
  enum AllocatorRegion {
    REGION_STACK, REGION_HEAP
  };

private:
  typedef std::set<MemoryObject *> objects_ty;

  struct ThreadMemorySegments {
    pseudoalloc::Mapping heap;
    pseudoalloc::Mapping stack;
  };

  objects_ty objects;
  ArrayCache *const arrayCache;

  std::map<ThreadId, ThreadMemorySegments> threadMemoryMappings;

  std::size_t threadHeapSize;
  std::size_t threadStackSize;
  std::size_t globalSegmentSize;

  pseudoalloc::Mapping globalMemorySegment;
  pseudoalloc::Alloc* globalAllocator;

  /**
   * Requested a memory mapping for `tid`.
   * If `requestedAddress` is unequal to `nullptr`, then the memory mapping is requested at
   * that address, otherwise the mapping at a random location
   */
  void initThreadMemoryMapping(const ThreadId& tid, std::size_t requestedAddress);

  pseudoalloc::Mapping createMapping(std::size_t size, std::uintptr_t requestedAddress);

  void loadRequestedThreadMemoryMappingsFromFile();

public:
  MemoryManager(ArrayCache *arrayCache);
  ~MemoryManager();

  /**
   * Returns memory object which contains a handle to real virtual process
   * memory.
   */
  MemoryObject *allocate(std::uint64_t size, bool isLocal, bool isGlobal,
                         const llvm::Value *allocSite, const Thread &thread,
                         std::size_t stackframeIndex, std::size_t alignment);

  MemoryObject *allocateFixed(std::uint64_t address, std::uint64_t size,
                              const llvm::Value *allocSite,
                              const Thread &thread,
                              std::size_t stackframeIndex);

  MemoryObject *allocateGlobal(std::uint64_t size, const llvm::Value *allocSite,
                               const ThreadId& byTid, std::size_t alignment);

  /**
   * Deallocates the memory at address `mo->address` in the allocator.
   *
   * Note: this does NOT free or invalidated the MemoryObject `mo`
   */
  void deallocate(const MemoryObject *mo, const Thread &thread);

  void markFreed(MemoryObject *mo);
  ArrayCache *getArrayCache() const { return arrayCache; }

  /**
   * Constructs a new thread allocator in the threads reserved
   * memory region.
   */
  pseudoalloc::Alloc* createThreadAllocator(const ThreadId &tid, AllocatorRegion region);

  void markMemoryRegionsAsUnneeded();
};

} // End klee namespace

#endif /* KLEE_MEMORYMANAGER_H */
