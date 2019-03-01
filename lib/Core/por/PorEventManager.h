#ifndef KLEE_POREVENTMANAGER_H
#define KLEE_POREVENTMANAGER_H

#include "klee/ExecutionState.h"
#include "klee/por/events.h"

namespace klee {
    class PorEventManager {
    public:
        PorEventManager() = default;

        bool registerPorEvent(ExecutionState &state, por_event_t kind, std::vector<std::uint64_t> args);
    private:
        static std::string getNameOfEvent(por_event_t kind);
    };
};

#endif //KLEE_POREVENTMANAGER_H
