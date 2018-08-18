#ifndef KLEE_PARTIALORDERGRAPH_H
#define KLEE_PARTIALORDERGRAPH_H

#include "klee/ExecutionState.h"

#include <vector>

namespace klee {
  class PartialOrderGraph {
    public:
      struct Node;

      struct ScheduleResult {
        ExecutionState* finishedState = nullptr;
        std::vector<ExecutionState*> newInactiveStates;
        std::vector<ExecutionState*> reactivatedStates;
        std::vector<ExecutionState*> newStates;
        std::vector<ExecutionState*> stoppedStates;
      };

    private:
      struct ScheduleDependency {
        Node* referencedNode;
        uint64_t scheduleIndex;
        uint8_t reason;
      };

      class Tree;

    public:
      /// @brief a node represents a schedule decision that happened after the parent nodes decisions
      struct Node {
          friend class PartialOrderGraph;
          friend class Tree;

        private:
          /// @brief used to step through the schedule serialization
          Node* parent = nullptr;

          /// @brief the hash for this scheduled step
          uint64_t dependencyHash = 0;

          /// @brief the thread that was scheduled
          Thread::ThreadId tid = 0;

          /// @brief a reference to the child in this tree
          Node* directChild = nullptr;

          uint64_t scheduleIndex = 0;

          /// @brief the dependencies that were found so far
          std::vector<ScheduleDependency> dependencies;

          // This is used for the subgraphing

          std::set<Thread::ThreadId> possibleOtherSchedules;

          ExecutionState* pausedState = nullptr;

          std::vector<Tree*> foreignTrees;

          Node() = default;
          // ~Node();
      };

    private:
      class Tree {
          friend class PartialOrderGraph;

        private:
          /// @brief this is the trees own root
          Node* root = nullptr;

          std::vector<Node*> scheduleHistory;

          /* Below is everything we need for being a forked tree */

          bool isFork = false;

          Tree* parentTree = nullptr;

          Node* forkedAtNode = nullptr;

          /* And the reasoning why we forked */

          Node* forkTriggerNode = nullptr;

          Tree() = default;
          Tree(Node* forkAtNode, Tree* parent);

          void registerEpochResult(ScheduleResult& result, ExecutionState* state);
      };

    private:
      Tree* rootTree = nullptr;

      std::map<ExecutionState*, Tree*> responsibleTrees;

    public:
      explicit PartialOrderGraph(ExecutionState* state);
      ~PartialOrderGraph();

      ScheduleResult registerEpochResult(ExecutionState* state);

      /// @brief dumps a graph of the tree
      void dump(llvm::raw_ostream &os);
  };

} // End klee namespace


#endif //KLEE_PARTIALORDERGRAPH_H
