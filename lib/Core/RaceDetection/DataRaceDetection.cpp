#include "DataRaceDetection.h"

#include "por/event/event.h"
#include "por/node.h"

#include "llvm/Support/CommandLine.h"

#include <iostream>
#include <iomanip>
#include <chrono>

using namespace klee;

namespace {
  llvm::cl::opt<bool> DebugDrd("debug-drd",
                               llvm::cl::desc("Outputs debug info on stderr about the data race detection"),
                               llvm::cl::init(false));

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

void DataRaceDetection::trackAccess(const por::node& node, const MemoryOperation& op) {
  assert(op.instruction != nullptr);
  assert(op.object != nullptr);
  assert(op.type != MemoryOperation::Type::UNKNOWN);
  assert(op.tid);
  assert(op.isAlloc() || op.isFree() || (op.numBytes != 0 && op.offset.get() != nullptr));

  auto evtIt = node.configuration().thread_heads().find(op.tid);
  assert(evtIt != node.configuration().thread_heads().end());

  auto& acc = getAccessesAfter(*evtIt->second);

  // in the case that a memory object is freed by KLEE, then we will not receive
  // any further operations on that object since the object won't be found (out of bound access)
  // -> we can prune all data that we tracked for that object
  if (op.isFree()) {
    acc.pruneDataForMemoryObject(op.object);
  } else {
    acc.trackMemoryOperation(op);
    stats.numTrackedAccesses++;
    globalStats.numTrackedAccesses++;
  }

  if (DebugDrd) {
    llvm::errs() << "DRD: @" << evtIt->second->to_string()
                 << " track> mo=" << getDebugInfo(op.object)
                 << " tid=" << op.tid
                 << " type=" << op.getTypeString()
                 << "\n";
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
  auto& curEventOfOperatingThread = it->second;

  std::vector<std::pair<ThreadId, const AccessMetaData&>> accessesToCheck;

  for (auto const &pair : threadsToCheck) {
    ThreadId const& tid = pair.first;
    if (tid == operation.tid) {
      continue;
    }

    por::event::event const *evt = pair.second;
    assert(evt != nullptr);

    for (; evt != nullptr; evt = evt->thread_predecessor()) {
      auto memAccesses = getAccessesAfter(*evt);
      if (auto accessed = memAccesses.getMemoryAccessesOfThread(operation.object); accessed.has_value()) {
        const auto& accessPatterns = accessed.value().get();

        assert(!accessPatterns.isAllocOrFree() && "Would have been tested in the fastpath");

        for (auto& access : accessPatterns.getAccesses()) {
          // check early since this is an easy pair
          if (operation.isConstantOffset() && access.isConstantOffset()) {
            continue;
          }

          if (access.isRead() && operation.isRead()) {
            continue;
          }

          // So if we could actually make a bounds check and the result
          // was that the accesses are not overlapping, then we do not
          // have to check this combination with the solver
          auto inBounds = operation.isOverlappingWith(access);
          if (inBounds.has_value() && !inBounds.value()) {
            continue;
          }

          accessesToCheck.emplace_back(tid, access);
        }
      }

      if (evt->is_less_than(*curEventOfOperatingThread)) {
        break;
      }
    }
  }

  assert(!accessesToCheck.empty() && "We have to have at least one pair to check");

  // So now we have to assemble a big query to only call the solver once
  ref<Expr> queryIsSafeForAll = ConstantExpr::create(1, Expr::Bool);
  for (auto& candidate : accessesToCheck) {
    // So the only way how the accesses can be safe if the bounds of these accesses are
    // not overlapping (e.g. the bounds are placed before or after another)
    // Either: operation: -xxx-------
    //         candidate: ------xxxx-
    //
    //     Or: operation: ------xxxx-
    //         candidate: -xxx-------

    auto endOfOp = operation.getOffsetOfLastAccessedByte();
    auto endOfCand = candidate.second.getOffsetOfLastAccessedByte();

    auto opBeforeCand = UltExpr::create(endOfOp, candidate.second.offset);
    auto candBeforeOp = UltExpr::create(endOfCand, operation.offset);

    auto condition = OrExpr::create(opBeforeCand, candBeforeOp);
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
  for (const auto& candidate : accessesToCheck) {
    auto endOfOp = operation.getOffsetOfLastAccessedByte();
    auto endOfCand = candidate.second.getOffsetOfLastAccessedByte();

    auto opBeforeCand = UltExpr::create(endOfOp, candidate.second.offset);
    auto candBeforeOp = UltExpr::create(endOfCand, operation.offset);

    auto notOverlapping = OrExpr::create(opBeforeCand, candBeforeOp);

    const auto& offsetsMatching = interface.mayBeFalse(notOverlapping);
    if (!offsetsMatching.has_value()) {
      // So the solver failed for this, but we still can try the other candidate
      continue;
    }

    if (offsetsMatching.value()) {
      result.racingThread = candidate.first;
      result.racingInstruction = candidate.second.instruction;

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

  RaceDetectionResult result;

  for (auto const &pair : threadsToCheck) {
    ThreadId const& tid = pair.first;
    if (tid == operation.tid) {
      continue;
    }

    por::event::event const *evt = pair.second;
    assert(evt != nullptr);

    for (; evt != nullptr; evt = evt->thread_predecessor()) {
      auto memAccesses = getAccessesAfter(*evt);
      if (auto accessed = memAccesses.getMemoryAccessesOfThread(operation.object); accessed.has_value()) {
        const auto& patterns = accessed.value().get();

        if (incomingIsAllocOrFree || patterns.isAllocOrFree()) {
          // We race with every other access, therefore simply pick the first
          result.racingInstruction = patterns.isAllocOrFree()
                                    ? patterns.getAllocFreeInstruction()
                                    : patterns.getAccesses().front().instruction;

          result.racingThread = tid;
          result.isRace = true;
          result.canBeSafe = false;
          return result;
        }

        // So we now know for sure that only standard accesses are inside here
        for (auto const& op : patterns.getAccesses()) {
          if (operation.isRead() && op.isRead()) {
            continue;
          }

          if (auto isOverlapping = operation.isOverlappingWith(op); isOverlapping.has_value()) {
            if (isOverlapping.value()) {
              result.racingInstruction = op.instruction;
              result.racingThread = tid;
              result.isRace = true;
              result.canBeSafe = false;
              return result;
            }
          } else {
            // We cannot give a definite result if during the check we encounter
            // a symbolic offset that is not matching and has to be checked by the solver.
            return {};
          }
        }
      }

      if (evt->is_less_than(*curEventOfOperatingThread)) {
        break;
      }
    }
  }

  result.isRace = false;
  result.hasNewConstraints = false;
  return result;
}
