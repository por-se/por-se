#pragma once

#include "CommonTypes.h"
#include "EpochMemoryAccesses.h"

#include <deque>
#include <map>

namespace por {
  namespace event {
    class event;
  }
  class node;
}

namespace klee {
  class DataRaceDetection {
    public:
      struct Stats {
        std::size_t numTrackedAccesses = 0;

        std::size_t numDataRacesChecks = 0;
        std::size_t numFastPathRaceChecks = 0;
        std::size_t numSolverRaceChecks = 0;

        std::uint64_t timeDataRaceChecks = 0;
        std::uint64_t timeFastPathChecks = 0;
        std::uint64_t timeSolverChecks = 0;
      };

    private:
      std::map<ThreadId, std::deque<std::pair<const por::event::event*, EpochMemoryAccesses>>> accesses;

      Stats stats;

    public:
      DataRaceDetection() = default;
      DataRaceDetection(const DataRaceDetection& drd) = default;

      void trackAccess(const por::node& node, const MemoryOperation& operation);

      std::optional<RaceDetectionResult>
      isDataRace(const por::node& node,
                 const SolverInterface &interface,
                 const MemoryOperation &operation);

      [[nodiscard]] const Stats& getStats() const;

      static const Stats& getGlobalStats();

    private:
      std::optional<RaceDetectionResult>
      SolverPath(const por::node& node,
                 const SolverInterface &interface,
                 const MemoryOperation& operation);

      /// The fast-path tries to check if the access is safe without using the solver
      std::optional<RaceDetectionResult>
      FastPath(const por::node& node,
               const MemoryOperation& operation);

      auto& getAccessListOfThread(const ThreadId& tid) {
        return accesses[tid];
      }

      auto& getAccessesAfter(const ThreadId& tid, por::event::event const* ev) {
        auto& accessList = accesses[tid];
        if (accessList.empty() || accessList.back().first != ev) {
          accessList.emplace_back(ev, EpochMemoryAccesses{});
        }

        return accessList.back().second;
      }
  };

  llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const DataRaceDetection::Stats &stats);
}
