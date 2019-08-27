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

#include <algorithm>
#include <fstream>
#include <unistd.h>
#include <utility>

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
              "Reserved memory for every threads stack size in GB (default=20)"),
      llvm::cl::init(20), llvm::cl::cat(MemoryCat));

llvm::cl::opt<std::string> ThreadSegmentsFile(
      "allocate-thread-segments-file",
      llvm::cl::desc("File that specifies the start addresses of thread segments"),
      llvm::cl::init(""), llvm::cl::cat(MemoryCat));
} // namespace

/***/
MemoryManager::MemoryManager(ArrayCache *_arrayCache)
    : arrayCache(_arrayCache) {

  auto pageSize = sysconf(_SC_PAGE_SIZE);

  threadHeapSize = static_cast<std::size_t>(ThreadHeapSize.getValue()) * 1024 * 1024 * 1024;
  threadStackSize = static_cast<std::size_t>(ThreadStackSize.getValue()) * 1024 * 1024 * 1024;

  // this assumes that the pagesize is a power of 2
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

  markMemoryRegionsAsUnneeded();
}

void MemoryManager::loadRequestedThreadMemoryMappingsFromFile() {
  std::ifstream segmentsFile(ThreadSegmentsFile);
  if (!segmentsFile.is_open()) {
    klee_error("Could not open the segments file specified by -allocate-thread-segments-file") ;
  }

  // Example content of the file: ```
  // # This line is a comment
  // 1 = 0x7ff30000000
  // 1.1 = 0x87c30000000 # all addresses have to be formatted as hex string
  //
  // ```

  auto lineNumber = 0;
  std::string line;
  while (getline(segmentsFile, line)) {
    lineNumber++;

    // Remove white space (not actually useful for parsing)
    line.erase(std::remove_if(line.begin(), line.end(), isspace), line.end());

    auto commentStart = line.find('#');
    if (commentStart != std::string::npos) {
      line.erase(commentStart);
    }

    if (line.empty())
      continue;

    auto delimiterPos = line.find('=');
    if (delimiterPos == std::string::npos) {
      klee_error("Line %d in -allocate-thread-segments-file malformed. Expected '='. Exiting.", lineNumber);
    }

    auto tidAsString = line.substr(0, delimiterPos);
    auto addressAsString = line.substr(delimiterPos + 1);

    auto forTid = ThreadId::from_string(tidAsString);
    if (!forTid) {
      klee_error("ThreadId in -allocate-thread-segments-file in line %d malformed. Exiting.", lineNumber);
    }

    assert(forTid->to_string() == tidAsString && "Parsed tid should be identical to the input one");

    // So this method below simply throws an exception if the string is not a formatted number.
    // Maybe we should use a better parsing method and provide more helpful feedback?
    std::uint64_t address = std::stoull(addressAsString, nullptr, 16);
    if (!address) {
      klee_error("Address specified in -allocate-thread-segments-file in line %d may not be zero. Exiting.",
                 lineNumber);
    }

    initThreadMemoryMapping(*forTid, address);
  }
}

void MemoryManager::initThreadMemoryMapping(const ThreadId& tid, std::size_t requestedAddress) {
  assert(threadMemoryMappings.find(tid) == threadMemoryMappings.end() && "Do not reinit a threads memory mapping");

  // Test that we do not place overlapping mappings by checking the requestedAddress
  // against the already existing mappings
  if (requestedAddress != 0) {
    auto end = requestedAddress + threadHeapSize + threadStackSize;

    for (const auto &seg : threadMemoryMappings) {
      // If new one is after the already created one
      if (seg.second.startAddress + seg.second.allocatedSize < requestedAddress) {
        continue;
      }

      // if the new one is before the already created one
      if (end < seg.second.startAddress) {
        continue;
      }

      // We overlap in some area - fail for now since otherwise threads can override the other threads data
      klee_error("Overlapping thread memory segments for tid1=%s and tid2=%s - Exiting.", seg.first.to_string().c_str(),
                 tid.to_string().c_str());
    }
  }

  pseudoalloc::Mapping threadHeapMapping{};
  if (requestedAddress != 0) {
    threadHeapMapping = pseudoalloc::pseudoalloc_new_mapping(requestedAddress, threadHeapSize);
  } else {
    threadHeapMapping = pseudoalloc::pseudoalloc_default_mapping(threadHeapSize);
  }

  if (threadHeapMapping.base == nullptr) {
    klee_error("Could not allocate thread heap memory for tid<%s> - error: %s", tid.to_string().c_str(),
               strerror(errno));
  }

  pseudoalloc::Mapping threadStackMapping{};
  if (requestedAddress != 0) {
    auto stackAddress = reinterpret_cast<uintptr_t>(requestedAddress) + threadHeapSize;

    threadStackMapping = pseudoalloc::pseudoalloc_new_mapping(stackAddress, threadStackSize);
  } else {
    threadStackMapping = pseudoalloc::pseudoalloc_default_mapping(threadStackSize);
  }

  if (threadStackMapping.base == nullptr) {
    klee_error("Could not allocate thread stack memory for tid<%s> - error: %s", tid.to_string().c_str(),
               strerror(errno));
  }

  ThreadMemorySegments segment{};
  segment.startAddress = reinterpret_cast<std::uint64_t>(threadHeapMapping.base);
  segment.allocatedSize = threadHeapSize + threadStackSize;

  segment.heap = threadHeapMapping;
  segment.stack = threadStackMapping;

  // Now check that the address is correct and that our mappings are of the correct size
  if (requestedAddress && segment.startAddress != requestedAddress) {
    klee_error("Could not allocate memory deterministically for tid<%s> at %p - received %p", tid.to_string().c_str(),
               reinterpret_cast<void*>(requestedAddress), threadHeapMapping.base);
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
    // Test that the address that we got is actually inside the mapping
    auto segmentIt = threadMemoryMappings.find(thread.getThreadId());
    assert(segmentIt != threadMemoryMappings.end() && "Thread has no known memory mapping");

    const auto& seg = isLocal ? segmentIt->second.stack : segmentIt->second.heap;
    auto base = reinterpret_cast<std::uint64_t>(seg.base);

    if (address < base || address > base + seg.len) {
      klee_error("Allocator returned an invalid address: address=0x%" PRIx64 ", start address of segment=0x%" PRIx64 ", length of segment=%" PRIu64, address, base, seg.len);
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

pseudoalloc::Alloc* MemoryManager::createThreadAllocator(const ThreadId &tid, AllocatorRegion region) {
  // So first of all, check if we already have a mapping for that thread
  auto it = threadMemoryMappings.find(tid);

  if (it == threadMemoryMappings.end()) {
    initThreadMemoryMapping(tid, 0);

    it = threadMemoryMappings.find(tid);
    assert(it != threadMemoryMappings.end() && "Threads memory mapping should be initialized");
  }

  auto allocator = pseudoalloc::pseudoalloc_new(region == REGION_STACK ? it->second.stack : it->second.heap);

  if (allocator == nullptr) {
    klee_error("Failed to create an allocator for tid=%s\n", tid.to_string().c_str());
  }

  return allocator;
}

void MemoryManager::markMemoryRegionsAsUnneeded() {
  for (auto& it : threadMemoryMappings) {
    pseudoalloc::pseudoalloc_dontneed(&it.second.heap);
    pseudoalloc::pseudoalloc_dontneed(&it.second.stack);
  }
}
