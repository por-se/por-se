//===-- MemoryManager.cpp -------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "MemoryManager.h"

#include "CoreStats.h"
#include "Memory.h"

#include "klee/Expr/Expr.h"
#include "klee/Internal/Support/ErrorHandling.h"

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"

#include <sys/mman.h>
#include <utility>

#include <unistd.h>
#include <fstream>
#include <iostream>

using namespace klee;

namespace {

llvm::cl::OptionCategory MemoryCat("Memory management options",
                                   "These options control memory management.");

llvm::cl::opt<bool> NullOnZeroMalloc(
    "return-null-on-zero-malloc",
    llvm::cl::desc("Returns NULL if malloc(0) is called (default=false)"),
    llvm::cl::init(false), llvm::cl::cat(MemoryCat));

llvm::cl::opt<unsigned> ThreadHeapSize(
    "allocate-thread-heap-size",
    llvm::cl::desc(
            "Reserved memory for every threads heap in GB (default=50)"),
    llvm::cl::init(50), llvm::cl::cat(MemoryCat));

llvm::cl::opt<unsigned> ThreadStackSize(
      "allocate-thread-stack-size",
      llvm::cl::desc(
              "Reserved memory for every threads stack size in GB (default=10)"),
      llvm::cl::init(10), llvm::cl::cat(MemoryCat));

llvm::cl::opt<std::string> ThreadSegmentsFile(
      "allocate-thread-segments-file",
      llvm::cl::desc("File that specifies the start addresses of thread segments"),
      llvm::cl::init(""), llvm::cl::cat(MemoryCat));
} // namespace

/***/
MemoryManager::MemoryManager(ArrayCache *_arrayCache)
    : arrayCache(_arrayCache) {

  auto pageSize = sysconf(_SC_PAGE_SIZE);

  threadHeapSize = static_cast<uintptr_t>(ThreadHeapSize.getValue()) * 1024 * 1024 * 1024;
  threadStackSize = static_cast<uintptr_t>(ThreadStackSize.getValue()) * 1024 * 1024 * 1024;

  if ((threadHeapSize & (pageSize - 1)) != 0) {
    klee_error("-allocate-thread-heap-size must be a multiple of the page size");
  }

  if ((threadStackSize & (pageSize - 1)) != 0) {
    klee_error("-allocate-thread-stack-size must be a multiple of the page size");
  }

  if (!ThreadSegmentsFile.empty()) {
    loadRequestedThreadMemoryMappingsFromFile();
  }
}

MemoryManager::~MemoryManager() {
  while (!objects.empty()) {
    MemoryObject *mo = *objects.begin();
    objects.erase(mo);
    delete mo;
  }

  for (auto& it : threadMemoryMappings) {
    pseudoalloc::pseudoalloc_dontneed(&it.second.heap);
    pseudoalloc::pseudoalloc_dontneed(&it.second.stack);
  }
}

void MemoryManager::loadRequestedThreadMemoryMappingsFromFile() {
  std::ifstream segmentsFile(ThreadSegmentsFile);
  if (!segmentsFile.is_open()) {
    klee_error("Could not open the segments file specified by -allocate-thread-segments-file") ;
  }

  std::string line;
  while (getline(segmentsFile, line)) {
    // Remove leading white space
    line.erase(std::remove_if(line.begin(), line.end(), isspace), line.end());

    if(line[0] == '#' || line.empty())
      continue;

    auto delimiterPos = line.find('=');
    auto tidAsString = line.substr(0, delimiterPos);
    auto addressAsString = line.substr(delimiterPos + 1);

    auto forTid = ThreadId::from_string(tidAsString);
    if (!forTid) {
      klee_error("ThreadId in -allocate-thread-segments-file malformed. Exiting.");
    }

    std::uint64_t address = std::stoull(addressAsString, nullptr, 16);
    if (!address) {
      klee_error("Address specified in -allocate-thread-segments-file may not be zero. Exiting.");
    }

    initThreadMemoryMapping(*forTid, reinterpret_cast<void*>(address));
  }
}

void MemoryManager::initThreadMemoryMapping(const ThreadId& tid, void* requestedAddress) {
  assert(threadMemoryMappings.find(tid) == threadMemoryMappings.end() && "Do not reinit a threads memory mapping");

#ifndef NDEBUG
  {
    // Test that we do not place overlapping mappings
    auto start = reinterpret_cast<std::uint64_t>(requestedAddress);
    auto end = start + threadHeapSize + threadStackSize;

    for (const auto& seg : threadMemoryMappings) {
      // If new one is after the already created one
      if (seg.second.startAddress + seg.second.allocatedSize < start) {
        continue;
      }

      // if the new one is before the already created one
      if (end < seg.second.startAddress) {
        continue;
      }

      // We overlap in some area - fail for now since otherwise threads can override the other threads data
      klee_error("Overlapping thread memory segments for tid1=%s and tid2=%s - Exiting.", seg.first.to_string().c_str(), tid.to_string().c_str());
    }
  }
#endif // NDEBUG

  pseudoalloc::Mapping threadHeapMapping{};
  if (requestedAddress != nullptr) {
    threadHeapMapping = pseudoalloc::pseudoalloc_new_mapping(requestedAddress, threadHeapSize);
  } else {
    threadHeapMapping = pseudoalloc::pseudoalloc_default_mapping(threadHeapSize);
  }

  pseudoalloc::Mapping threadStackMapping{};
  if (requestedAddress != nullptr) {
    auto stackAddress = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(requestedAddress) + threadHeapSize);

    threadStackMapping = pseudoalloc::pseudoalloc_new_mapping(stackAddress, threadStackSize);
  } else {
    threadStackMapping = pseudoalloc::pseudoalloc_default_mapping(threadStackSize);
  }

  ThreadMemorySegments segment{};
  segment.startAddress = reinterpret_cast<std::uint64_t>(threadHeapMapping.base);
  segment.allocatedSize = threadHeapSize + threadStackSize;

  segment.heap = threadHeapMapping;
  segment.stack = threadStackMapping;

#ifndef NDEBUG
  {
    if (requestedAddress && threadHeapMapping.base != requestedAddress) {
      klee_error("Could not allocate memory deterministically for tid<%s> at %p - received %p", tid.to_string().c_str(),
                 requestedAddress, threadHeapMapping.base);
    }

    if (threadHeapMapping.len != threadHeapSize) {
      klee_error(
              "Allocator failed to create the heap mapping with the requested size: requested size=%lu, returned size=%lu",
              threadHeapSize, threadHeapMapping.len);
    }

    if (threadStackMapping.len != threadStackSize) {
      klee_error(
              "Allocator failed to create the stack mapping with the requested size: requested size=%lu, returned size=%lu",
              threadStackSize, threadStackMapping.len);
    }
  }
#endif // NDEBUG

  auto insertOpRes = threadMemoryMappings.insert(std::make_pair(tid, segment));
  assert(insertOpRes.second && "Mapping should always be able to be registered");

  klee_message("Created thread memory mapping for tid<%s> at %p", tid.to_string().c_str(), threadHeapMapping.base);
}

MemoryObject *MemoryManager::allocate(std::uint64_t size,
                                      bool isLocal,
                                      bool isGlobal,
                                      const llvm::Value *allocSite,
                                      const Thread &thread,
                                      std::size_t stackframeIndex,
                                      std::size_t alignment) {
  if (size > 10 * 1024 * 1024)
    klee_warning_once(nullptr, "Large alloc: %" PRIu64
                               " bytes.  KLEE may run out of memory.",
                               size);

  // Return NULL if size is zero, this is equal to error during allocation
  if (NullOnZeroMalloc && size == 0)
    return nullptr;

  if (!llvm::isPowerOf2_64(alignment)) {
    klee_warning("Only alignment of power of two is supported");
    return nullptr;
  }

  void* allocAddress;
  if (isLocal) {
    allocAddress = pseudoalloc::pseudoalloc_alloc_aligned(thread.threadStackAlloc, size, alignment);
  } else {
    allocAddress = pseudoalloc::pseudoalloc_alloc_aligned(thread.threadHeapAlloc, size, alignment);
  }

  auto address = reinterpret_cast<std::uint64_t>(allocAddress);

#ifndef NDEBUG
  {
    auto segmentIt = threadMemoryMappings.find(thread.getThreadId());
    assert(segmentIt != threadMemoryMappings.end() && "Thread has no known memory mapping");

    const auto& seg = isLocal ? segmentIt->second.stack : segmentIt->second.heap;
    auto base = reinterpret_cast<std::uint64_t>(seg.base);

    if (address < base || address > base + seg.len) {
      klee_error("Allocator returned an invalid address: address=0x%llx, start address of segment=0x%llx, length of segment=%lu", address, base, seg.len);
    }
  }
#endif // NDEBUG

  if (!address)
    return nullptr;

  ++stats::allocations;
  MemoryObject *res =
    new MemoryObject(address, size, isLocal, isGlobal, false, allocSite,
                     std::make_pair(thread.getThreadId(), stackframeIndex), this);
  objects.insert(res);
  return res;
}

MemoryObject *MemoryManager::allocateFixed(std::uint64_t address, std::uint64_t size,
                                           const llvm::Value *allocSite,
                                           const Thread &thread,
                                           std::size_t stackframeIndex) {
#ifndef NDEBUG
  for (objects_ty::iterator it = objects.begin(), ie = objects.end(); it != ie;
       ++it) {
    MemoryObject *mo = *it;
    if (address + size > mo->address && address < mo->address + mo->size)
      klee_error("Trying to allocate an overlapping object");
  }
#endif

  ++stats::allocations;
  MemoryObject *res =
    new MemoryObject(address, size, false, true, true, allocSite,
                     std::make_pair(thread.getThreadId(), stackframeIndex), this);
  objects.insert(res);
  return res;
}

void MemoryManager::deallocate(const MemoryObject *mo, const Thread &thread) {
  // so we have to pass this info to the allocator
  auto address = reinterpret_cast<void*>(mo->address);
  if (mo->isLocal) {
    pseudoalloc::pseudoalloc_dealloc(thread.threadStackAlloc, address);
  } else {
    pseudoalloc::pseudoalloc_dealloc(thread.threadHeapAlloc, address);
  }
}

void MemoryManager::markFreed(MemoryObject *mo) {
  if (objects.find(mo) != objects.end()) {
    objects.erase(mo);
  }
}

size_t MemoryManager::getUsedDeterministicSize() {
  // TODO: needs support in pseudoalloc
  return 0;
}

pseudoalloc::Alloc* MemoryManager::createThreadAllocator(const ThreadId &tid, AllocatorRegion region) {
  // So first of all, check if we already have a mapping for that thread
  auto it = threadMemoryMappings.find(tid);

  if (it == threadMemoryMappings.end()) {
    initThreadMemoryMapping(tid, nullptr);

    it = threadMemoryMappings.find(tid);
    assert(it != threadMemoryMappings.end() && "Threads memory mapping should be initialized");
  }

  return pseudoalloc::pseudoalloc_new(region == REGION_STACK ? it->second.stack : it->second.heap);
}

void MemoryManager::markMemoryRegionsAsUnneeded() {
  for (auto& it : threadMemoryMappings) {
    pseudoalloc::pseudoalloc_dontneed(&it.second.heap);
    pseudoalloc::pseudoalloc_dontneed(&it.second.stack);
  }
}