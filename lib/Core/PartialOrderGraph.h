#ifndef KLEE_PARTIALORDERGRAPH_H
#define KLEE_PARTIALORDERGRAPH_H

#include "klee/ExecutionState.h"

#include <vector>

namespace klee {
  class PartialOrderGraph {
    public:
      typedef std::function <ExecutionState* (ExecutionState*)> StateForkProvider;

      struct Node;

      struct ScheduleResult {
        /// @brief the state that is now completely discovered
        ExecutionState* finishedState = nullptr;

        /// @brief states that are added in this step, but are not active for now
        std::vector<ExecutionState*> newInactiveStates;

        /// @brief states that we activated again after being inactive
        std::vector<ExecutionState*> reactivatedStates;

        /// @brief states that were added
        std::vector<ExecutionState*> newStates;

        /// @brief states that we do no longer need
        std::vector<ExecutionState*> stoppedStates;
      };

    private:
      struct ScheduleDependency {
        Node* referencedNode;
        uint64_t scheduleIndex;
        uint8_t reason;
      };

      struct PartialOrdering {
        Node* dependent;
        Node* dependency;
      };

      class Tree;

    public:
      /// @brief a node represents a schedule decision that happened after the parent nodes decisions
      struct Node {
          friend class PartialOrderGraph;

        private:
          friend class Tree;

          /// @brief used to step through the schedule serialization
          Node* parent = nullptr;

          /// @brief the hash for this scheduled step
          uint64_t dependencyHash = 0;

          /// @brief the thread that was scheduled
          Thread::ThreadId tid = 0;

          /// @brief a reference to the child in this tree
          Node* directChild = nullptr;

          /// @brief the index of this node in the schedule history
          uint64_t scheduleIndex = 0;

          /// @brief the dependencies that were found so far
          std::vector<ScheduleDependency> dependencies;

          // This is used in order to support forks of this tree

          /// @brief all threads that are possible to execute as an alternative to the chosen tid in the next node
          std::set<Thread::ThreadId> possibleOtherSchedules;

          /// @brief this is the state that was the result of the execution that was represented by this node
          ExecutionState* resultingState = nullptr;

          /// @brief all other schedules that were started from this node
          std::vector<Tree*> foreignTrees;

          Node() = default;
          ~Node();
      };

    private:
      class Tree {
          friend class PartialOrderGraph;

        private:
          /// @brief this is the trees own root
          Node* root = nullptr;

          /// @brief our history in this fork
          std::vector<Node*> scheduleHistory;

          /* Below is everything we need for being a forked tree */

          /// @brief if unable to null, then this is the tree that we are a fork from
          Tree* parentTree = nullptr;

          /// @brief a reference to the actual graph that this tree is part of
          PartialOrderGraph* graph = nullptr;

          /* And the reasoning why we forked */

          /// @brief this is a link to the two nodes in the parent tree that caused this fork
          PartialOrdering* forkReason = nullptr;

          /// @brief a reference to the node that should now have another dependency set
          Node* changedNode = nullptr;

          /// @brief a schedule we should follow for as long as possible
          std::vector<Node*> shadowSchedule;

          /// @brief the current index in the shadow schedule that we have to mimic in this fork
          uint64_t shadowScheduleIterator = 0;

          Tree() = default;

          /// @brief will use the node as the parent of this trees root and will register the tree as parent
          Tree(Node* forkAtNode, Tree* parent);
          ~Tree();

          /// @brief checks if it is possible (based on schedule dependencies) to change the schedule order
          bool checkIfPermutable(Node *dependency, Node *dependent);

          /// @brief will add all results that are important to the scheduling and reactive forks
          void registerEpochResult(ScheduleResult& result, ExecutionState* state);

          /// @brief will determine the next thread that should be executed by this tree
          void scheduleNextThread(ScheduleResult& result, ExecutionState* state);

          /// @brief will activate the fork that this dependency hints at
          std::pair<PartialOrderGraph::Tree*, ExecutionState*>
          activateScheduleFork(Tree* base, Node *triggerNode, ScheduleDependency *dep, ScheduleResult &result);

          /// @brief will use the data of the latest node in order to find necessary forks
          std::vector<std::pair<Tree*, ExecutionState*>> checkForNecessaryForks(ScheduleResult& result);
      };

    protected:
      /// @brief a function that will be used in order to fork the states
      StateForkProvider forkProvider;

    private:
      /// @brief the root tree that started everything
      Tree* rootTree = nullptr;

      /// @brief a map of all active trees and their corresponding states
      std::map<ExecutionState*, Tree*> responsibleTrees;

    public:
      /// @brief starts an po graph with this state as the basis
      explicit PartialOrderGraph(ExecutionState* state);
      PartialOrderGraph(ExecutionState *state, StateForkProvider &provider);
      ~PartialOrderGraph();

      /// @brief adds all data from the state and will return all resulting schedule changes
      ScheduleResult processEpochResult(ExecutionState *state);

      /// @brief dumps a graph of the tree
      void dump(llvm::raw_ostream &os);
  };

} // End klee namespace


#endif //KLEE_PARTIALORDERGRAPH_H
