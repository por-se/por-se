#include "ScheduleTree.h"

using namespace klee;

ScheduleTree::ScheduleTree() {
  root = new Node();
}

ScheduleTree::Node* ScheduleTree::getNodeOfExecutionState(ExecutionState *state) {
  auto search = activeNodes.find(state);
  if (search == activeNodes.end()) {
    return nullptr;
  }

  return search->second;
}

bool ScheduleTree::hasMatchingPermutations(Node *base, std::set<uint64_t> hashes, Node *ignore, uint64_t stillNeeded) {
  for (auto n : base->children) {
    if (n.second == ignore) {
      continue;
    }

    if (hashes.find(n.second->dependencyHash) == hashes.end()) {
      // If there is a hash that is not in our selection, then it is impossible
      // to find in the subtree a matching schedule
      continue;
    }

    // Hey here is a match
    if (stillNeeded == 1) {
      // So we found the last missing hash: that means we can return with the current
      // node without checking the other ones. It is simply impossible to have another
      // child with the same hash

      return true;
    } else {
      return hasMatchingPermutations(n.second, hashes, nullptr, stillNeeded - 1);
    }
  }

  return false;
}

bool ScheduleTree::findPermutations(ExecutionState *state) {
  auto search = activeNodes.find(state);
  assert(search != activeNodes.end() && "No such state in the tree");

  std::vector<ScheduleTree::Node*> results;

  Node* pre = search->second;
  Node* n = pre->parent;
  std::set<uint64_t> availableHashes;
  availableHashes.insert(pre->dependencyHash);
  uint64_t stillNeeded = 1;

  while (n != nullptr) {
    bool found = hasMatchingPermutations(n, availableHashes, pre, stillNeeded);
    if (found) {
      return true;
    }

    stillNeeded++;
    availableHashes.insert(n->dependencyHash);
    pre = n;
    n = n->parent;
  }

  return false;
}