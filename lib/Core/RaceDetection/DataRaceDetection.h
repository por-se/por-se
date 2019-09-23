#pragma once

#include <deque>
#include <unordered_map>
#include <por/configuration.h>

#include "CommonTypes.h"

#include "EpochMemoryAccesses.h"

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
      std::unordered_map<const por::event::event*, EpochMemoryAccesses> accessesV2;

      Stats stats;

    public:
      DataRaceDetection() = default;
      DataRaceDetection(const DataRaceDetection& drd) = default;

      void trackAccess(const std::unique_ptr<por::configuration>& cfg, const MemoryOperation& operation);

      std::optional<RaceDetectionResult>
      isDataRace(const std::unique_ptr<por::configuration>& cfg,
                 const SolverInterface &interface,
                 const MemoryOperation &operation);

      [[nodiscard]] const Stats& getStats() const;

      static const Stats& getGlobalStats();

    private:
      std::optional<RaceDetectionResult>
      isDataRaceSolver(const std::unique_ptr<por::configuration>& cfg,
                       const SolverInterface &interface,
                       const MemoryOperation& operation);

      /// The fast-path tries to check if the access is safe without using the solver
      std::optional<RaceDetectionResult>
      isDataRaceFastPath(const std::unique_ptr<por::configuration>& cfg,
                         const MemoryOperation& operation);

      EpochMemoryAccesses& getAccessesAfter(const por::event::event& evt) {
        auto it = accessesV2.find(&evt);
        if (it != accessesV2.end()) {
          return it->second;
        }

        auto insertIt = accessesV2.emplace(&evt, EpochMemoryAccesses{});
        assert(insertIt.second);
        return insertIt.first->second;
      };
  };

  llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const DataRaceDetection::Stats &stats);
}