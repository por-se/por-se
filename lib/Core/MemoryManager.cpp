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

llvm::cl::opt<unsigned> GlobalSegmentSize(
      "allocate-global-segment-size",
      llvm::cl::desc(
              "Reserved memory for globals in GB (default=10)"),
      llvm::cl::init(10), llvm::cl::cat(MemoryCat));

llvm::cl::opt<unsigned> GlobalROSegmentSize(
      "allocate-global-read-only-segment-size",
      llvm::cl::desc(
              "Reserved memory for read-only globals in GB (default=10)"),
      llvm::cl::init(10), llvm::cl::cat(MemoryCat));

llvm::cl::opt<std::string> ThreadSegmentsFile(
      "allocate-thread-segments-file",
      llvm::cl::desc("File that specifies the start addresses of thread segments"),
      llvm::cl::init(""), llvm::cl::cat(MemoryCat));

llvm::cl::opt<std::uint32_t> QuarantineSize(
      "allocate-quarantine",
      llvm::cl::desc("Size of quarantine queues in allocator (default=8, also see -allocate-quarantine-unlimited)"),
      llvm::cl::init(8), llvm::cl::cat(MemoryCat));

llvm::cl::opt<bool> UnlimitedQuarantine(
      "allocate-quarantine-unlimited",
      llvm::cl::desc("Never reuse free'd addresses. (default=off)"),
      llvm::cl::init(false), llvm::cl::cat(MemoryCat));
} // namespace

std::uint32_t MemoryManager::quarantine;

/***/
MemoryManager::MemoryManager(ArrayCache *_arrayCache)
    : arrayCache(_arrayCache) {

  auto pageSize = sysconf(_SC_PAGE_SIZE);

  threadHeapSize = static_cast<std::size_t>(ThreadHeapSize.getValue()) * 1024 * 1024 * 1024;
  threadStackSize = static_cast<std::size_t>(ThreadStackSize.getValue()) * 1024 * 1024 * 1024;
  globalSegmentSize = static_cast<std::size_t>(GlobalSegmentSize.getValue()) * 1024 * 1024 * 1024;
  globalROSegmentSize = static_cast<std::size_t>(GlobalSegmentSize.getValue()) * 1024 * 1024 * 1024;

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

  if (!globalMemorySegment) {
    globalMemorySegment = createMapping(globalSegmentSize, 0);
  }

  if (!globalROMemorySegment) {
    globalROMemorySegment = createMapping(globalROSegmentSize, 0);
  }

  quarantine = UnlimitedQuarantine ? pseudoalloc::allocator_t::unlimited_quarantine : QuarantineSize;
  if (quarantine == pseudoalloc::allocator_t::unlimited_quarantine) {
    klee_message("Using unlimited quarantine for allocator.");
    if (QuarantineSize.getNumOccurrences() > 0) {
      klee_error("-allocate-quarantine cannot be used with -allocate-quarantine-unlimited");
    }
  }

  globalAllocator = pseudoalloc::allocator_t(globalMemorySegment, quarantine);
  globalROAllocator = pseudoalloc::allocator_t(globalROMemorySegment, quarantine);
}

MemoryManager::~MemoryManager() {
  globalObjectsMap.clear();
  while (!objects.empty()) {
    MemoryObject *mo = *objects.begin();
    objects.erase(mo);
    delete mo;
  }

  markMemoryRegionsAsUnneeded();
  globalROMemorySegment.clear();
}

void MemoryManager::outputConfig(std::unique_ptr<llvm::raw_fd_ostream>&& out) {
  configOut = std::move(out);

  // Also directly write the already present info
  auto& os = *configOut;

  os
    << "global = " << globalMemorySegment.begin() << '\n'
    << "globalRO = " << globalROMemorySegment.begin() << '\n';

  for (const auto& [ tid, seg ] : threadMemoryMappings) {
    void* heap_ptr = seg.heap.begin();
    void* stack_ptr = seg.stack.begin();

    os << tid << " : heap = " << heap_ptr << '\n';
    os << tid << " : stack = " << stack_ptr << '\n';
  }

  os.flush();
}

void MemoryManager::loadRequestedThreadMemoryMappingsFromFile() {
  std::ifstream segmentsFile(ThreadSegmentsFile);
  if (!segmentsFile.is_open()) {
    klee_error("Could not open the segments file specified by -allocate-thread-segments-file") ;
  }

  // Example content of the file: ```
  // # This line is a comment
  // global = 0x7ff30000000
  // globalRO = 0x82c30000000 # all addresses have to be formatted as hex string
  // 1 : stack = 0x90c30000000
  // 1,1 : heap = 0x98c30000000 
  //
  // ```

  std::map<ThreadId, std::pair<std::uint64_t, std::uint64_t>> threadAddresses;

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

    auto entryName = line.substr(0, delimiterPos);
    auto addressAsString = line.substr(delimiterPos + 1);

    // So this method below simply throws an exception if the string is not a formatted number.
    // Maybe we should use a better parsing method and provide more helpful feedback?
    std::uint64_t address = std::stoull(addressAsString, nullptr, 16);
    if (!address) {
      klee_error("Address specified in -allocate-thread-segments-file in line %d may not be zero. Exiting.",
                 lineNumber);
    }

    if (entryName == "global") {
      globalMemorySegment = createMapping(globalSegmentSize, address);
      klee_message("Created memory mapping for read-write globals at %p", globalMemorySegment.begin());
    } else if (entryName == "globalRO") {
      globalROMemorySegment = createMapping(globalROSegmentSize, address);
      klee_message("Created memory mapping for read-only globals at %p", globalROMemorySegment.begin());
    } else {
      auto tidDelimiter = entryName.find(':');
      if (tidDelimiter == std::string::npos) {
        klee_error("Line %d in -allocate-thread-segments-file malformed. Expected ':' for a thread mappings line. Exiting.", lineNumber);
      }

      auto tidAsString = entryName.substr(0, tidDelimiter);
      auto type = entryName.substr(tidDelimiter + 1);

      bool isHeap = true;

      if (type == "stack") {
        isHeap = false;
      } else if (type == "heap") {
        isHeap = true;
      } else {
        klee_error("Line %d in -allocate-thread-segments-file malformed. Expected either stack or thread. Exiting.", lineNumber);
      }

      auto forTid = ThreadId::from_string(tidAsString);
      if (!forTid) {
        klee_error("ThreadId in -allocate-thread-segments-file in line %d malformed. Exiting.", lineNumber);
      }

      assert(forTid->to_string() == tidAsString && "Parsed tid should be identical to the input one");

      auto& entry = threadAddresses[forTid.value()];
      
      if (isHeap) {
        entry.first = address;
      } else {
        entry.second = address;
      }
    }
  }

  for (const auto& [ tid, addresses ] : threadAddresses) {
    initThreadMemoryMapping(tid, addresses.first, addresses.second);
  }
}

pseudoalloc::mapping_t MemoryManager::createMapping(std::size_t size, std::uintptr_t requestedAddress) {
  auto reqAddressAsPointer = reinterpret_cast<void*>(requestedAddress);

  // Test that we do not place overlapping mappings by checking the requestedAddress
  // against the already existing mappings
  if (requestedAddress != 0) {
    auto reqEnd = requestedAddress + size;

    if (globalMemorySegment) {
      auto begin = reinterpret_cast<std::uintptr_t>(globalMemorySegment.begin());

      if (!(reqEnd <= begin || requestedAddress > begin + globalMemorySegment.size())) {
        klee_error("Overlapping mapping requested=%p size=0x%zx and other=%p (global read-write) - Exiting.",
                reqAddressAsPointer, size, globalMemorySegment.begin());
      }
    }

    if (globalROMemorySegment) {
      auto begin = reinterpret_cast<std::uintptr_t>(globalROMemorySegment.begin());

      if (!(reqEnd <= begin || requestedAddress > begin + globalROMemorySegment.size())) {
        klee_error("Overlapping mapping requested=%p size=0x%zx and other=%p (global read-only) - Exiting.",
                reqAddressAsPointer, size, globalROMemorySegment.begin());
      }
    }

    for (const auto &[tid, seg] : threadMemoryMappings) {
      auto heapBegin = reinterpret_cast<std::uintptr_t>(seg.heap.begin());
      auto stackBegin = reinterpret_cast<std::uintptr_t>(seg.stack.begin());

      if (!(reqEnd <= heapBegin || requestedAddress > heapBegin + seg.heap.size())) {
        klee_error("Overlapping mapping requested=%p size=0x%zx and other=%p (heap) - Exiting.",
                reqAddressAsPointer, size, seg.heap.begin());
      }

      if (!(reqEnd <= stackBegin || requestedAddress > stackBegin + seg.stack.size())) {
        klee_error("Overlapping mapping requested=%p size=0x%zx and other=%p (stack) - Exiting.",
                   reqAddressAsPointer, size, seg.stack.begin());
      }
    }
  }

  pseudoalloc::mapping_t mapping;
  if (requestedAddress != 0) {
    mapping = pseudoalloc::mapping_t(requestedAddress, size);
  } else {
    mapping = pseudoalloc::mapping_t(size);
  }

  if (!mapping) {
    klee_error("Could not allocate a mapping at %p - error: %s", reqAddressAsPointer, strerror(errno));
  }

  // Now check that the address is correct and that our mappings are of the correct size
  if (requestedAddress && mapping.begin() != reqAddressAsPointer) {
    klee_error("Could not allocate a mapping at %p - received %p", reqAddressAsPointer, mapping.begin());
  }

  if (mapping.size() != size) {
    klee_error(
            "Allocator failed to create a mapping with the requested size: requested size=%lu, returned size=%lu",
            size, mapping.size());
  }

  return mapping;
}

void MemoryManager::initThreadMemoryMapping(const ThreadId& tid, std::uintptr_t reqHeap, std::uintptr_t reqStack) {
  assert(threadMemoryMappings.find(tid) == threadMemoryMappings.end() && "Do not reinit a threads memory mapping");

  ThreadMemorySegments segment{
    .heap = createMapping(threadHeapSize, reqHeap),
    .stack = createMapping(threadStackSize, reqStack)
  };

  auto [it, res] = threadMemoryMappings.emplace(tid, std::move(segment));
  assert(res && "Mapping should always be able to be registered");

  klee_message(
    "Created thread memory mapping for thread %s at heap=%p stack=%p",
    tid.to_string().c_str(),
    it->second.heap.begin(),
    it->second.stack.begin()
  );

  if (configOut) {
    auto& os = *configOut;

    os << tid << " : heap = " << it->second.heap.begin() << '\n';
    os << tid << " : stack = " << it->second.stack.begin() << '\n';
    os.flush();
  }
}

MemoryObject *MemoryManager::allocate(std::uint64_t size,
                                      bool isLocal,
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
    allocAddress = thread.threadStackAlloc->allocate(std::max(size, alignment));
  } else {
    allocAddress = thread.threadHeapAlloc->allocate(std::max(size, alignment));
  }

  auto address = reinterpret_cast<std::uint64_t>(allocAddress);

  if (!address)
    return nullptr;

#ifndef NDEBUG
  {
    // Test that the address that we got is actually inside the mapping
    auto segmentIt = threadMemoryMappings.find(thread.getThreadId());
    assert(segmentIt != threadMemoryMappings.end() && "Thread has no known memory mapping");

    const auto& seg = isLocal ? segmentIt->second.stack : segmentIt->second.heap;
    auto base = reinterpret_cast<std::uint64_t>(seg.begin());

    if (address < base || address > base + seg.size()) {
      klee_error("Allocator returned an invalid address: address=0x%" PRIx64 ", start address of segment=0x%" PRIx64 ", length of segment=%" PRIu64, address, base, seg.size());
    }
  }
#endif // NDEBUG

  ++stats::allocations;
  MemoryObject *res =
          new MemoryObject(address, size, alignment, isLocal, false, false, false, allocSite,
                           std::make_pair(thread.getThreadId(), stackframeIndex), this);
  objects.insert(res);
  return res;
}


MemoryObject *MemoryManager::allocateGlobal(std::uint64_t size,
                                            const llvm::Value *allocSite,
                                            const ThreadId& byTid,
                                            std::size_t alignment,
                                            bool readOnly) {
  // Return NULL if size is zero, this is equal to error during allocation
  if (NullOnZeroMalloc && size == 0)
    return nullptr;

  if (!llvm::isPowerOf2_64(alignment)) {
    klee_warning("Only alignment of power of two is supported");
    return nullptr;
  }

  void* allocAddress;
  if (readOnly) {
    allocAddress = globalROAllocator.allocate(std::max(size, alignment));
  } else {
    allocAddress = globalAllocator.allocate(std::max(size, alignment));
  }
  auto address = reinterpret_cast<std::uint64_t>(allocAddress);

  if (!address)
    return nullptr;

#ifndef NDEBUG
  {
    // Test that the address that we got is actually inside the mapping
    std::uint64_t base;
    std::size_t size;
    if (readOnly) {
      const auto& seg = globalROMemorySegment;
      base = reinterpret_cast<std::uint64_t>(seg.begin());
      size = seg.size();
    } else {
      const auto& seg = globalMemorySegment;
      base = reinterpret_cast<std::uint64_t>(seg.begin());
      size = seg.size();
    }

    if (address < base || address > base + size) {
      klee_error("Allocator returned an invalid address: address=0x%" PRIx64 ", start address of segment=0x%" PRIx64 ", length of segment=%" PRIu64, address, base, size);
    }
  }
#endif // NDEBUG

  ++stats::allocations;
  auto res = new MemoryObject(address, size, alignment, false, true, false, false, allocSite,
                           std::make_pair(byTid, 0), this);
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
          new MemoryObject(address, size, 0, false, true, true, false, allocSite,
                           std::make_pair(thread.getThreadId(), stackframeIndex), this);
  objects.insert(res);
  return res;
}

void MemoryManager::deallocate(const MemoryObject *mo, const Thread &thread) {
  assert(mo->getAllocationStackFrame().first == thread.tid);

  // so we have to pass this info to the allocator
  auto address = reinterpret_cast<void*>(mo->address);
  if (mo->isLocal) {
    thread.threadStackAlloc->free(address, std::max(mo->size, mo->alignment));
  } else {
    thread.threadHeapAlloc->free(address, std::max(mo->size, mo->alignment));
  }
}

void MemoryManager::markFreed(MemoryObject *mo) {
  if (objects.find(mo) != objects.end()) {
    objects.erase(mo);
  }
}

MemoryManager::ThreadMemorySegments& MemoryManager::getThreadSegments(const ThreadId& tid) {
  // So first of all, check if we already have a mapping for that thread
  auto it = threadMemoryMappings.find(tid);

  if (it == threadMemoryMappings.end()) {
    initThreadMemoryMapping(tid, 0, 0);

    it = threadMemoryMappings.find(tid);
    assert(it != threadMemoryMappings.end() && "Threads memory mapping should be initialized");
  }

  return it->second;
}

std::unique_ptr<pseudoalloc::allocator_t> MemoryManager::createThreadHeapAllocator(const ThreadId &tid) {
  auto& seg = getThreadSegments(tid);
  return std::make_unique<pseudoalloc::allocator_t>(seg.heap, quarantine);
}

std::unique_ptr<pseudoalloc::stack_allocator_t> MemoryManager::createThreadStackAllocator(const ThreadId &tid) {
  auto& seg = getThreadSegments(tid);
  return std::make_unique<pseudoalloc::stack_allocator_t>(seg.stack, quarantine);
}

void MemoryManager::markMemoryRegionsAsUnneeded() {
  for (auto& it : threadMemoryMappings) {
    it.second.heap.clear();
    it.second.stack.clear();
  }

  globalMemorySegment.clear();
}
