#include "DataRaceDetection.h"

#include "klee/OptionCategories.h"

#include "por/event/event.h"
#include "por/node.h"
#include "util/iterator_range.h"

#include "llvm/Support/CommandLine.h"

#include <iomanip>
#include <chrono>
#include <utility>
#include <tuple>
#include <vector>

using namespace klee;

namespace {
  llvm::cl::opt<bool> DebugDrd("debug-drd",
                               llvm::cl::desc("Outputs debug info on stderr about the data race detection"),
                               llvm::cl::init(false),
                               llvm::cl::cat(DebugCat));

  DataRaceDetection::Stats globalStats;
}

static std::string getDebugInfo(const MemoryObject* mo) {
  auto area = mo->isLocal ? 'L' : (mo->isGlobal ? 'G' : 'H');

  std::stringstream stream;
  stream
      << "0x"
      << std::setfill('0') << std::setw(sizeof(std::uint64_t) * 2)
      << std::hex << mo->address
      << " [" << area << "]";

  return stream.str();
}

llvm::raw_ostream &klee::operator<<(llvm::raw_ostream &os, const DataRaceDetection::Stats &stats) {
  return os
    << "{\n"
    << "  \"numTrackedAccesses\": " << stats.numTrackedAccesses << ",\n"

    << "  \"numDataRacesChecks\": " << stats.numDataRacesChecks << ",\n"
    << "  \"numFastPathRaceChecks\": " << stats.numFastPathRaceChecks << ",\n"
    << "  \"numSolverRaceChecks\": " << stats.numSolverRaceChecks << ",\n"

    << "  \"timeDataRaceChecks\": " << stats.timeDataRaceChecks << ",\n"
    << "  \"timeFastPathChecks\": " << stats.timeFastPathChecks << ",\n"
    << "  \"timeSolverChecks\": " << stats.timeSolverChecks << "\n"
    << "}";
}

const DataRaceDetection::Stats& DataRaceDetection::getGlobalStats() {
  return globalStats;
}

const DataRaceDetection::Stats& DataRaceDetection::getStats() const {
  return stats;
}

void DataRaceDetection::trackAccess(const por::node& node, MemoryOperation&& op) {
  assert(op.instruction != nullptr);
  assert(op.object != nullptr);
  assert(op.type != MemoryOperation::Type::UNKNOWN);
  assert(op.tid);
  assert(op.isAlloc() || op.isFree() || (op.numBytes != 0 && op.offset.get() != nullptr));

  auto evtIt = node.configuration().thread_heads().find(op.tid);
  assert(evtIt != node.configuration().thread_heads().end());

  auto& acc = getAccessesAfter(op.tid, evtIt->second);

  if (DebugDrd) {
    llvm::errs() << "DRD: @" << evtIt->second->kind()
                 << " track> mo=" << getDebugInfo(op.object)
                 << " tid=" << op.tid
                 << " type=" << op.getTypeString()
                 << "\n";
  }

  // in the case that a memory object is freed by KLEE, then we will not receive
  // any further operations on that object since the object won't be found (out of bound access)
  // -> we can prune all data that we tracked for that object
  if (op.isFree()) {
    acc.pruneDataForMemoryObject(op.object);
  } else {
    acc.trackMemoryOperation(std::move(op));
    stats.numTrackedAccesses++;
    globalStats.numTrackedAccesses++;
  }
}

std::optional<RaceDetectionResult>
DataRaceDetection::isDataRace(const por::node& node,
                              const SolverInterface &interface,
                              const MemoryOperation& operation) {
  RaceDetectionResult finalResult;

  stats.numDataRacesChecks++;
  globalStats.numDataRacesChecks++;

  auto clockStart = std::chrono::steady_clock::now();

  // Test if we can try a fast path -> races with a concrete offset or alloc/free
  const auto easyResult = FastPath(node, operation);

  if (easyResult.has_value()) {
    // So if the fast path could produce any definite claims, then either
    // the mo was not accessed or it was an easy race (concrete offset or alloc/free)

    if (DebugDrd) {
      auto& r = easyResult.value();

      llvm::errs() << "DRD: @" << node.configuration().size()
                   << " check> mo=" << getDebugInfo(operation.object)
                   << " tid=" << operation.tid
                   << " type=" << operation.getTypeString()
                   << " race=" << r.isRace << " [fast-path]"
                   << "\n";
    }

    stats.numFastPathRaceChecks++;
    globalStats.numFastPathRaceChecks++;

    auto end = std::chrono::steady_clock::now();
    auto dur = std::chrono::duration_cast<std::chrono::nanoseconds>(end - clockStart).count();

    stats.timeDataRaceChecks += dur;
    stats.timeFastPathChecks += dur;
    globalStats.timeDataRaceChecks += dur;
    globalStats.timeFastPathChecks += dur;

    return easyResult;
  }

  const auto& solverResult = SolverPath(node, interface, operation);

  stats.numSolverRaceChecks++;
  globalStats.numSolverRaceChecks++;

  auto end = std::chrono::steady_clock::now();
  auto dur = std::chrono::duration_cast<std::chrono::nanoseconds>(end - clockStart).count();

  stats.timeDataRaceChecks += dur;
  stats.timeSolverChecks += dur;
  globalStats.timeDataRaceChecks += dur;
  globalStats.timeSolverChecks += dur;

  if (DebugDrd) {
    if (!solverResult.has_value()) {
      llvm::errs() << "DRD: @" << node.configuration().size()
                   << " check> mo=" << getDebugInfo(operation.object)
                   << " tid=" << operation.tid
                   << " type=" << operation.getTypeString()
                   << " race=unknown (solver failure)"
                   << "\n";
    } else if (!solverResult->isRace) {
      llvm::errs() << "DRD: @" << node.configuration().size()
                   << " check> mo=" << getDebugInfo(operation.object)
                   << " tid=" << operation.tid
                   << " type=" << operation.getTypeString()
                   << " race=0 [solver]"
                   << "\n";
    } else {
      llvm::errs() << "DRD: @" << node.configuration().size()
                   << " check> mo=" << getDebugInfo(operation.object)
                   << " tid=" << operation.tid
                   << " type=" << operation.getTypeString()
                   << " race=symbolic-dependent [solver]"
                   << " canBeSafe=" << solverResult->canBeSafe
                   << "\n";
    }
  }

  return solverResult;
}

std::optional<RaceDetectionResult>
DataRaceDetection::SolverPath(const por::node& node,
                              const SolverInterface &interface,
                              const MemoryOperation &operation) {
  // So we have to check if we have potentially raced with any thread
  const auto& threadsToCheck = node.configuration().thread_heads();
  auto it = threadsToCheck.find(operation.tid);
  assert(it != threadsToCheck.end());
  auto* curEventOfOperatingThread = it->second;

  std::vector<std::tuple<ThreadId, ref<Expr>, const AccessMetaData&>> accessesToCheck;

  for (auto const &pair : threadsToCheck) {
    ThreadId const& tid = pair.first;
    if (tid == operation.tid) {
      continue;
    }

    por::event::event const *evt = pair.second;
    assert(evt != nullptr);
    // The `succ` event has the `evt` as the thread predecessor.
    // -> It is the direct thread successor of `evt`.
    por::event::event const *succ = nullptr;

    const auto& accessList = getAccessListOfThread(tid);
    for (auto accessListIt = accessList.rbegin(), accessListEnd = accessList.rend(); accessListIt != accessListEnd; ++accessListIt) {
      // skip all events that do not have any registered memory accesses
      while (evt != nullptr && evt != accessListIt->first) {
        succ = evt;
        evt = evt->thread_predecessor();
      }
      if (evt == nullptr) {
        break;
      }

      // Since accesses are associated with the event that happened *before* they did, the memory accesses associated with
      // the first event that is less than the one we are looking at still need to be checked. Any accesses happening even
      // before that are synchronized.
      if (succ && succ->is_less_than(*curEventOfOperatingThread)) {
        break;
      }

      const auto& memAccesses = accessListIt->second;
      if (auto* accessed = memAccesses.getMemoryAccessesOfThread(operation.object)) {
        assert(!accessed->isAllocOrFree() && "Should have caused a datarace on the fastpath");

        if (isa<ConstantExpr>(operation.offset)) {
          // Any operation that happens at a concrete offset will have been checked against all other concrete operations
          // on the fastpath pass. Thus, we can omit iterating over `accessed->getConcreteAccesses`.

          for (const auto& [accessOffset, access] : accessed->getSymbolicAccesses()) {
            if (operation.isWrite() || access.isWrite()) {
              accessesToCheck.emplace_back(tid, accessOffset, access);
            }
          }
        } else {
          for (const auto& [accessOffset, access] : accessed->getConcreteAccesses()) {
            if (operation.isWrite() || access.isWrite()) {
              accessesToCheck.emplace_back(tid, Expr::createPointer(accessOffset), access);
            }
          }
          for (const auto& [accessOffset, access] : accessed->getSymbolicAccesses()) {
            if (operation.isWrite() || access.isWrite()) {
              assert(operation.offset != accessOffset && "checked in fastpath");
              accessesToCheck.emplace_back(tid, accessOffset, access);
            }
          }
        }
      }
    }
  }

  assert(!accessesToCheck.empty() && "We have to have at least one pair to check");

  // So now we have to assemble a big query to only call the solver once
  ref<Expr> queryIsSafeForAll = ConstantExpr::create(1, Expr::Bool);
  auto endOfOp = AddExpr::create(Expr::createZExtToPointerWidth(operation.offset), Expr::createPointer(operation.numBytes - 1));
  for (auto& [tid, accessOffset, access] : accessesToCheck) {
    // So the only way how the accesses can be safe if the bounds of these accesses are
    // not overlapping (e.g. the bounds are placed before or after another)
    // Either: operation: -xxx-------
    //         candidate: ------xxxx-
    //
    //     Or: operation: ------xxxx-
    //         candidate: -xxx-------

    auto endOfAccess = AddExpr::create(Expr::createZExtToPointerWidth(accessOffset), Expr::createPointer(access.numBytes - 1));

    auto opBeforeCand = UltExpr::create(endOfOp, Expr::createZExtToPointerWidth(accessOffset));
    auto accBeforeOp = UltExpr::create(endOfAccess, Expr::createZExtToPointerWidth(operation.offset));

    auto condition = OrExpr::create(opBeforeCand, accBeforeOp);
    queryIsSafeForAll = AndExpr::create(queryIsSafeForAll, condition);
  }

  // First test is whether the access is always safe (e.g. for all symbolic expr)
  const auto& isAlwaysSafeAccess = interface.mustBeTrue(queryIsSafeForAll);
  if (!isAlwaysSafeAccess.has_value()) {
    return {};
  }

  RaceDetectionResult result;

  if (isAlwaysSafeAccess.value()) {
    // So we know for sure that the access is safe (since the offsets never match)
    result.isRace = false;
    result.hasNewConstraints = true;
    result.newConstraints = queryIsSafeForAll;

    return result;
  }

  // We have a data race in any case, but there are still two situations:
  // -> we race for every choice for the symbolic values
  // -> depending on the choice we have a race or we have no race
  result.isRace = true;

  // First check if we race every time (e.g. there is no assignment to symbolic values where the offsets are unequal)
  const auto& canBeSafe = interface.mayBeTrue(queryIsSafeForAll);
  if (!canBeSafe.has_value()) {
    return {};
  }

  if (canBeSafe.value()) {
    // We now know that the offsets
    result.canBeSafe = true;
    result.conditionToBeSafe = queryIsSafeForAll;
  }

  // Now find the actual racing instruction, we know that at least one exists
  for (auto& [tid, accessOffset, access] : accessesToCheck) {
    auto endOfAccess = AddExpr::create(Expr::createZExtToPointerWidth(accessOffset), Expr::createPointer(access.numBytes - 1));

    auto opBeforeCand = UltExpr::create(endOfOp, Expr::createZExtToPointerWidth(accessOffset));
    auto accBeforeOp = UltExpr::create(endOfAccess, Expr::createZExtToPointerWidth(operation.offset));

    auto notOverlapping = OrExpr::create(opBeforeCand, accBeforeOp);

    const auto& offsetsMatching = interface.mayBeFalse(notOverlapping);
    if (!offsetsMatching.has_value()) {
      // So the solver failed for this, but we still can try the other candidate
      continue;
    }

    if (offsetsMatching.value()) {
      result.racingThread = tid;
      result.racingInstruction = access.instruction;

      return result;
    }
  }

  // Fall through, so we cannot exactly tell if this is actually a race since the solver could not
  // tell us a second time whether there is a race
  return {};
}

std::optional<RaceDetectionResult>
DataRaceDetection::FastPath(const por::node& node,
                            const MemoryOperation& operation) {
  // So we have to check if we have potentially raced with any thread
  const auto& threadsToCheck = node.configuration().thread_heads();
  auto it = threadsToCheck.find(operation.tid);
  assert(it != threadsToCheck.end());
  auto& curEventOfOperatingThread = it->second;

  auto incomingIsAllocOrFree = operation.isAlloc() || operation.isFree();

  std::optional<RaceDetectionResult> result{std::in_place_t{}};
  result->isRace = false;
  result->hasNewConstraints = false;

  for (auto const &pair : threadsToCheck) {
    ThreadId const& tid = pair.first;
    if (tid == operation.tid) {
      continue;
    }

    por::event::event const *evt = pair.second;
    assert(evt != nullptr);
    // The `succ` event has the `evt` as the thread predecessor.
    // -> It is the direct thread successor of `evt`.
    por::event::event const *succ = nullptr;

    auto& accessList = getAccessListOfThread(tid);
    for (auto accessListIt = accessList.rbegin(), accessListEnd = accessList.rend(); accessListIt != accessListEnd; ++accessListIt) {
      // skip all events that do not have any registered memory accesses
      while (evt != nullptr && evt != accessListIt->first) {
        succ = evt;
        evt = evt->thread_predecessor();
      }
      if (evt == nullptr) {
        break;
      }

      // Since accesses are associated with the event that happened *before* they did, the memory accesses associated with
      // the first event that is less than the one we are looking at still need to be checked. Any accesses happening even
      // before that are synchronized.
      if (succ && succ->is_less_than(*curEventOfOperatingThread)) {
        break;
      }

      const auto& memAccesses = accessListIt->second;
      if (auto* accessed = memAccesses.getMemoryAccessesOfThread(operation.object)) {
        if (incomingIsAllocOrFree || accessed->isAllocOrFree()) {
          result.emplace();
          // We race with every other access, therefore simply pick the first
          result->racingInstruction = accessed->isAllocOrFree()
                                    ? accessed->getAllocFreeInstruction()
                                    : accessed->getAnyAccess().instruction;

          result->racingThread = tid;
          result->isRace = true;
          result->canBeSafe = false;
          return result;
        }

        // So we now know for sure that only standard accesses are inside here
        if (auto operationOffsetExpr = dyn_cast<ConstantExpr>(operation.offset)) {
          auto operationOffset = operationOffsetExpr->getZExtValue();
          // for operations with concrete offset, we can only check against other concrete offsets
          auto it = accessed->getConcreteAccesses().lower_bound(operationOffset);
          if (!(it != accessed->getConcreteAccesses().end() && it->first == operationOffset) 
            && it != accessed->getConcreteAccesses().begin()) {
            auto prev = std::prev(it);
            if (operation.isWrite() || prev->second.isWrite()) {
              // assert(prev->first < operationOffset);
              if(prev->first + prev->second.numBytes > operationOffset) {
                result.emplace();
                result->racingInstruction = prev->second.instruction;
                result->racingThread = tid;
                result->isRace = true;
                result->canBeSafe = false;
                return result;
              }
            }
          }
          for (; it != accessed->getConcreteAccesses().end()
            && it->first < operationOffset + operation.numBytes; ++it) {
            // assert(it->first >= operationOffset);
            if (operation.isWrite() || it->second.isWrite()) {
              result.emplace();
              result->racingInstruction = it->second.instruction;
              result->racingThread = tid;
              result->isRace = true;
              result->canBeSafe = false;
              return result;
            }
          }

          if (result.has_value()) {
            for (auto &[accessOffset, access] : accessed->getSymbolicAccesses()) {
              if (operation.isWrite() || access.isWrite()) {
                result.reset();
                break;
              }
            }
          }
        } else {
          auto [begin, end] = accessed->getSymbolicAccesses().equal_range(operation.offset);
          // for operations with symbolic offset, we can only check against other symbolic offsets
          for (const auto& [offset, access] : util::make_iterator_range(begin, end)) {
            if (operation.isRead() && access.isRead()) {
              continue;
            }

            // all operations overlap at this point, as their offset expressions are equal
            // and they always have more than one byte each
            // assert(offset == incoming.offset);
            // assert(incoming.numBytes > 0 && operation.numBytes > 0);
            result.emplace();
            result->racingInstruction = access.instruction;
            result->racingThread = tid;
            result->isRace = true;
            result->canBeSafe = false;
            return result;
          }
          if (result.has_value()) {
            for (auto &[accessOffset, access] : util::make_iterator_range(accessed->getSymbolicAccesses().begin(), begin)) {
              if (operation.isWrite() || access.isWrite()) {
                result.reset();
                break;
              }
            }
          }
          if (result.has_value()) {
            for (auto &[accessOffset, access] : util::make_iterator_range(end, accessed->getSymbolicAccesses().end())) {
              if (operation.isWrite() || access.isWrite()) {
                result.reset();
                break;
              }
            }
          }
          
          if (result.has_value()) {
            for (auto &[accessOffset, access] : accessed->getConcreteAccesses()) {
              if (operation.isWrite() || access.isWrite()) {
                result.reset();
                break;
              }
            }
          }
        }
      }
    }
  }

  return result;
}
