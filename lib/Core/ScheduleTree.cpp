#include "ScheduleTree.h"

#include <string>

using namespace klee;

ScheduleTree::ScheduleTree(ExecutionState* state) {
  root = new Node();
  activeNodes[state] = root;
}

ScheduleTree::~ScheduleTree() {
  // So go ahead and delete all nodes
  activeNodes.clear();

  delete root;
  root = nullptr;
}

ScheduleTree::Node::~Node() {
  for (auto n : children) {
    delete n;
  }

  children.clear();
  parent = nullptr;
}

ScheduleTree::Node* ScheduleTree::getNodeOfExecutionState(ExecutionState *state) {
  auto search = activeNodes.find(state);
  if (search == activeNodes.end()) {
    return nullptr;
  }

  return search->second;
}

bool ScheduleTree::hasEquivalentScheduleStep(Node *base, std::set<uint64_t> &hashes, Node *ignore, uint64_t stillNeeded,
                                             std::set<uint64_t>& sThreads) {
  for (auto n : base->children) {
    if (n == ignore) {
      continue;
    }

    bool scheduledThread = sThreads.find(n->scheduledThreadNumber) != sThreads.end();
    if (!scheduledThread) {
      // Even if there was a thread scheduled that we did not have in the current
      // list, we can merge them still if they had no interference
      bool found = hasEquivalentScheduleStep(n, hashes, nullptr, stillNeeded, sThreads);
      if (found) {
        return true;
      }

      // If there is nothing in the subtree then we do not want to let the other checks run as well
      continue;
    }

    if (hashes.find(n->dependencyHash) == hashes.end()) {
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
      bool found = hasEquivalentScheduleStep(n, hashes, nullptr, stillNeeded - 1, sThreads);
      if (found) {
        return true;
      }
    }
  }

  return false;
}

void ScheduleTree::registerSchedulingResult(ExecutionState* state) {
  Node* n = getNodeOfExecutionState(state);
  assert(n != nullptr && "There should be an active node in the tree matching the state");

  n->dependencyHash = state->dependencyHashes.back();

  // If we have a result, then the state is no longer active
  activeNodes.erase(state);
}

void ScheduleTree::pruneState(Node *pruneNode) {
  pruneNode->parent->children.erase(pruneNode);

  // We want to clear all parent nodes that have only one child.
  // This reduces the tree in general and makes it more slim
  Node* n = pruneNode->parent;
  while (n != nullptr && n->parent != nullptr) {
    Node* newN = n->parent;

    if (n->children.empty()) {
      n->parent->children.erase(n);
      delete n;
    }

    n = newN;
  }
}

void ScheduleTree::unregisterState(ExecutionState* state) {
  Node* n = getNodeOfExecutionState(state);
  if (n == nullptr) {
    return;
  }

  activeNodes.erase(state);

  // if this node does not provide any important info then just clear it
  if (n->dependencyHash == 0 && n->parent != nullptr) {
    n->parent->children.erase(n);
    delete n;
  }
}

void ScheduleTree::registerNewChild(Node *base, ExecutionState *newState) {
  assert(base != nullptr && "Base node must be available");

  Node* newNode = new Node();
  newNode->parent = base;
  newNode->scheduledThreadNumber = newState->getCurrentThreadReference()->getThreadNumber();

  base->children.insert(newNode);
  activeNodes[newState] = newNode;
}

bool ScheduleTree::hasEquivalentSchedule(Node* node) {
  if (node->parent == nullptr || node->parent->parent == nullptr) {
    // Fast path: for an effective permutation we have to have at least
    // two layers above the current node in the tree
    return false;
  }

  assert(node != nullptr && node->dependencyHash != 0 && "The node should already be explored");

  Node* childToIgnore = node->parent;
  Node* searchBase = childToIgnore->parent;
  uint64_t stillNeeded = 2;

  std::set<uint64_t> availableHashes;
  std::set<uint64_t> scheduleThreads;

  availableHashes.insert(node->dependencyHash);
  availableHashes.insert(childToIgnore->dependencyHash);

  scheduleThreads.insert(node->scheduledThreadNumber);
  scheduleThreads.insert(childToIgnore->scheduledThreadNumber);

  while (searchBase != nullptr) {
    bool found = hasEquivalentScheduleStep(searchBase, availableHashes, childToIgnore, stillNeeded, scheduleThreads);
    if (found) {
      return true;
    }

    stillNeeded++;
    availableHashes.insert(searchBase->dependencyHash);
    scheduleThreads.insert(searchBase->scheduledThreadNumber);

    childToIgnore = searchBase;
    searchBase = searchBase->parent;
  }

  return false;
}

void ScheduleTree::dump(llvm::raw_ostream &os) {
  os << "digraph G {\n";
  os << "\tsize=\"10,7.5\";\n";
  os << "\tratio=fill;\n";
  os << "\tcenter = \"true\";\n";
  os << "\tnode [style=\"filled\",width=.1,height=.1,fontname=\"Terminus\"]\n";
  os << "\tedge [arrowsize=.5]\n";

  std::vector<Node*> stack;
  stack.push_back(root);

  while (!stack.empty()) {
    Node* n = stack.front();
    stack.erase(stack.begin());

    os << "\tn" << n << "[label=\"" << (n->dependencyHash & 0xFFFF) << " [" << n->scheduledThreadNumber << "]\"];\n";
    if (n->parent != nullptr) {
      os << "\tn" << n->parent << " -> n" << n << ";\n";
    }

    stack.insert(stack.end(), n->children.begin(), n->children.end());
  }
  os << "}\n";
}