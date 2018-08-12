#ifndef KLEE_SCHEDULETREE_H
#define KLEE_SCHEDULETREE_H

#include "klee/ExecutionState.h"

#include <vector>
#include <map>
#include <set>

namespace klee {
  class Executor;

  class ScheduleTree {
    friend class Executor;

    public:
      struct Node {
        Node* parent;
        ExecutionState* state;

        uint64_t dependencyHash;

        std::map<Thread::ThreadId, Node*> children;
      };

    private:
      Node* root;
      std::map<ExecutionState*, Node*> activeNodes;

      bool hasMatchingPermutations(Node *base, std::set<uint64_t> hashes, Node *ignore, uint64_t stillNeeded);

    public:
      ScheduleTree();

      Node* getNodeOfExecutionState(ExecutionState* state);

      /// @brief tries to find permutations in the tree based on the given state
      bool findPermutations(ExecutionState *state);
  };

} // End klee namespace

#endif //KLEE_SCHEDULETREE_H
