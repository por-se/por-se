#ifndef KLEE_PARTIALORDER_EXPLORER_H
#define KLEE_PARTIALORDER_EXPLORER_H

#include "klee/ExecutionState.h"

#include <vector>

namespace klee {
  class PartialOrderExplorer {
    public:
      typedef std::function <ExecutionState* (ExecutionState*)> StateForkProvider;

      class Node;

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
        uint8_t reason;
      };

      /// @brief this struct represents a ordering in a partial order
      struct OrderingRelation {
        /// @brief thread id of the thread that should be executed first
        Thread::ThreadId lowerTid;

        /// @brief thread id of the thread that should be executed after the `lowerTid`
        Thread::ThreadId higherTid;

        // Note: we use the predecessors to specify which thread we want to execute in which order
        // The reason is that if we change the order than the actual targeted threads (and their dependencies)
        // will change and then we can no longer reliable tell them apart

        /// @brief the predecessor of the node that we want to execute first
        Node* lowerPredNode;

        /// @brief the predecessor of the node that we want to execute after the other
        Node* higherPredNode;

        OrderingRelation* reverse() {
          auto o = new OrderingRelation();
          o->lowerPredNode = higherPredNode;
          o->lowerTid = higherTid;

          o->higherPredNode = lowerPredNode;
          o->higherTid = lowerTid;

          return o;
        }

        bool matchesHigher(Node* pre) const {
          if (pre == nullptr && higherPredNode == nullptr) {
            return true;
          }

          if (pre == nullptr || higherPredNode == nullptr) {
            return false;
          }

          return pre->dependencyHash == higherPredNode->dependencyHash;
        }

        bool matchesLower(Node* pre) const {
          if (pre == nullptr && lowerPredNode == nullptr) {
            return true;
          }

          if (pre == nullptr || lowerPredNode == nullptr) {
            return false;
          }

          return pre->dependencyHash == lowerPredNode->dependencyHash;
        }

        bool isReverse(OrderingRelation *rel) {
          return rel->lowerTid == higherTid &&
                 rel->higherTid == lowerTid &&
                 matchesLower(rel->higherPredNode) &&
                 matchesHigher(rel->lowerPredNode);
        }

        friend bool operator==(const OrderingRelation &lhs, const OrderingRelation &rhs) {
          return lhs.lowerTid == rhs.lowerTid &&
                 lhs.higherTid == rhs.higherTid &&
                 lhs.matchesLower(rhs.lowerPredNode) &&
                 lhs.matchesHigher(rhs.higherPredNode);
        }

        friend bool operator!=(const OrderingRelation &lhs, const OrderingRelation &rhs) {
          return !(rhs == lhs);
        }
      };

      class Path;
      class MultiPath;

    public:
      /// @brief a node represents a schedule decision that happened after the parent nodes decisions
      class Node {
          friend class PartialOrderExplorer;

        private:
          friend class MultiPath;
          friend class Path;

          /// @brief used to step through the schedule serialization
          Node* parent = nullptr;

          /// @brief the hash for this scheduled step
          uint64_t dependencyHash = 0;

          /// @brief the thread that was scheduled
          Thread::ThreadId tid = 0;

          /// @brief the index of this node in the schedule history
          uint64_t scheduleIndex = 0;

          /// @brief the dependencies that were found so far
          std::vector<ScheduleDependency> dependencies;

          Path* path = nullptr;

          // This is used in order to support forks of this tree

          /// @brief all threads that are possible to execute as an alternative to the chosen tid in the next node
          std::set<Thread::ThreadId> possibleOtherSchedules;

          /// @brief this is the state that was the result of the execution that was represented by this node
          ExecutionState* resultingState = nullptr;

          /// @brief all restrictions that are based on this node (aka lower)
          std::vector<OrderingRelation*> anchoredRestrictions;

          Node() = default;
          ~Node();

          void registerAnchoredRestriction(OrderingRelation* rel);
      };

    private:
      class Path {
          friend class PartialOrderExplorer;
          friend class MultiPath;

        private:
          Node* root = nullptr;

          /// @brief our history in this fork
          std::vector<Node*> scheduleHistory;

          MultiPath* parentMultiPath = nullptr;

          MultiPath* resultingMultiPath = nullptr;

          PartialOrderExplorer* graph = nullptr;

          Node* forkReason = nullptr;

          uint64_t counter = 0;

          /// @brief if the whole tree is now ready and everything is not cleaned
          bool finished = false;

          /// @brief the ordering restrictions that we have gotten from the previous tree
          std::vector<OrderingRelation*> restrictions;

          std::vector<OrderingRelation*> allRestrictions;

          Path() = default;

          void registerRestriction(OrderingRelation* rel, bool newOne);

          /// @brief will split this path at the node of the specified schedule index
          Path* splitPathAt(uint64_t scheduleIndex);

          /// @brief tries to find the predecessor of this thread execution of the same thread
          Node* findPredecessor(Node* base);

          /// @brief tries to find the last finished thread execution
          Node* getLastThreadExecution(Thread::ThreadId tid);

          Node* createNewNode();

          /// @brief will add all results that are important to the scheduling and reactive forks
          void registerEpochResult(ScheduleResult& result, ExecutionState* state);

          /// @brief checks if this thread can now be executed
          bool checkIfScheduleable(Thread::ThreadId tid, ExecutionState* state);

          /// @brief will determine the next thread that should be executed by this tree
          void scheduleNextThread(ExecutionState* state, std::set<Thread::ThreadId> tid);

          /// @brief checks if it is possible (based on schedule dependencies) to change the schedule order
          bool checkIfPermutable(Node *dependency, Node *dependent);
      };

      class MultiPath {
          friend class PartialOrderExplorer;

        private:
          Path* parentPath = nullptr;

          std::vector<Path*> children;

          /// @brief how many active leafs can be reached based off this multi path
          uint64_t activeLeafs = 0;

          MultiPath() = default;

          MultiPath(Path* parent, uint64_t splitAt);

          Path* createNewPath();
      };

    protected:
      /// @brief a function that will be used in order to fork the states
      StateForkProvider forkProvider;

    private:
      /// @brief the root tree that started everything
      Path* rootPath = nullptr;

      /// @brief a map of all active trees and their corresponding states
      std::map<ExecutionState*, Path*> responsiblePaths;
      std::map<Path*, ExecutionState*> responsiblePathsReverse;

      bool mergeWithFork(ScheduleResult &result, Path *base, OrderingRelation *rel);

      bool checkIfIndependentFork(ScheduleResult& result, Node* triggerNode, ScheduleDependency* dep);

      void setupFork(ScheduleResult& result, Node* triggerNode, ScheduleDependency* dep);

      void checkForNecessaryForksV2(ScheduleResult& result, Path* path);

      void checkForNecessaryForks(ScheduleResult& result, Path* path);

      /// @brief will clean up all trees that are starting based of this tree
      // void cleanUpPath(Path *base, ScheduleResult &result);

    public:
      /// @brief starts an po graph with this state as the basis
      PartialOrderExplorer(ExecutionState *state, StateForkProvider &provider);
      // ~PartialOrderExplorer();

      /// @brief adds all data from the state and will return all resulting schedule changes
      ScheduleResult processEpochResult(ExecutionState *state);

      /// @brief dumps a graph of the tree
      void dump(llvm::raw_ostream &os);
  };

} // End klee namespace


#endif //KLEE_PARTIALORDER_EXPLORER_H
