#ifndef KLEE_SCHEDULETREE_H
#define KLEE_SCHEDULETREE_H

#include "klee/ExecutionState.h"

#include <set>
#include <vector>

namespace klee {
  /// @brief tree to record all already processed schedules and to search for equivalent ones
  class ScheduleTree {
    public:
      class Node;

    private:
      struct ScheduleDependency {
        Node* referencedNode;
        uint64_t scheduleIndex;
        uint8_t reason;
      };

      enum NodeType {
        UNDEFINED,
        SCHEDULING,
        SYMBOLIC
      };

    public:
      /// @brief a node represents a schedule decision that happened after the parent nodes decisions
      class Node {
        friend class ScheduleTree;

        private:
          // @brief the type why this node exists
          NodeType type = UNDEFINED;

          /// @brief used to step through the tree
          Node* parent = nullptr;

          /// @brief all nodes that are nested
          std::vector<Node*> children;

          // @brief the symbolic expression why we forked
          ref<Expr> symbolicExpression = nullptr;

          /// @brief the hash for this scheduled step
          uint64_t dependencyHash = 0;

          /// @brief the thread that was scheduled as a
          Thread::ThreadId tid = 0;

          uint64_t scheduleIndex = 0;

          /// @brief the dependencies that were found so far
          std::vector<ScheduleDependency> dependencies;

          Node() = default;
          ~Node();
      };

    private:
      /// @brief the root of the tree
      Node* root;

      /// @brief nodes that are marked as currently processed
      std::map<ExecutionState*, Node*> activeNodes;

      bool hasEquivalentScheduleStep(Node *base, std::set<uint64_t> &hashes, Node *ignore, uint64_t stillNeeded,
                                     std::vector<ref < Expr>> &constraints);

      /// @brief will add a new state to the schedule tree
      Node* registerNewChild(Node *base, ExecutionState *newState, ref<Expr> expr);

    public:
      explicit ScheduleTree(ExecutionState* state);
      ~ScheduleTree();

      /// @brief will return the corresponding node for the state
      Node* getNodeOfExecutionState(ExecutionState* state);

      /// @brief removes the state out of the schedule tree without adding any results
      void unregisterState(ExecutionState* state);

      /// @brief will remove the node out of the scheduling tree and mark the schedule as pruned
      void pruneState(Node *pruneNode);

      /// @brief register the processed data into the schedule tree and removes the state
      void registerSchedulingResult(ExecutionState* state);

      void registerScheduleDecision(Node *base, std::vector<ExecutionState *>& newStates);

      void registerSymbolicForks(Node *base, std::vector<ExecutionState *> &newStates,
                                 const std::vector<ref < Expr>> &expression);

      /// @brief tests if there is an equivalent schedule in the tree
      bool hasEquivalentSchedule(Node* node);

      /// @brief dumps a graph of the tree
      void dump(llvm::raw_ostream &os);
  };

} // End klee namespace

#endif //KLEE_SCHEDULETREE_H
