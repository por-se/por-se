#include "ScheduleTree.h"

#include <string>

using namespace klee;

static bool isInConstraints(std::vector<ref<Expr>> &constraints, ref<Expr> ref) {
  for (auto e : constraints) {
    if (*ref == *e) {
      return true;
    }
  }

  return false;
}

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
                                             std::vector<ref<Expr>> &constraints) {
  assert(stillNeeded != 0 && "We always have to find at least one");

  for (auto n : base->children) {
    if (n == ignore) {
      continue;
    }

    if (n->dependencyHash == 0) {
      if (n->symbolicExpression.isNull() || n->children.empty()) {
        continue;
      }

      if (!isInConstraints(constraints, n->symbolicExpression)) {
        continue;
      }
    } else if (hashes.find(n->dependencyHash) == hashes.end()) {
      // If there is a hash that is not in our selection, then it is impossible
      // to find in the subtree a matching schedule
      continue;
    }

    if (!n->symbolicExpression.isNull() && !isInConstraints(constraints, n->symbolicExpression)) {
      continue;
    }

    // Hey here is a match
    if (stillNeeded == 1) {
      // So we found the last missing hash: that means we can return with the current
      // node without checking the other ones. It is simply impossible to have another
      // child with the same hash

      if (n->dependencyHash != 0) {
        return true;
      }
    } else {
      bool found = hasEquivalentScheduleStep(n, hashes, nullptr, stillNeeded - 1, constraints);
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

  n->dependencyHash = state->schedulingHistory.back().dependencyHash;
  auto& deps = state->getCurrentEpochDependencies()->dependencies;
  n->dependencies.reserve(deps.size());

  uint64_t cur = state->schedulingHistory.size() - 1;

  for (auto& dep : deps) {
    ScheduleDependency d{};
    d.reason = dep.reason;
    d.scheduleIndex = dep.scheduleIndex;

    d.referencedNode = n;
    for (uint64_t i = 0; i < (cur - d.scheduleIndex); i++) {
      d.referencedNode = d.referencedNode->parent;
    }

    n->dependencies.push_back(d);
  }

  // If we have a result, then the state is no longer active
  activeNodes.erase(state);
}

void ScheduleTree::pruneState(Node *pruneNode) {
  while (pruneNode->parent != nullptr && pruneNode->parent->children.size() == 1) {
    pruneNode = pruneNode->parent;
  }

  if (pruneNode->parent != nullptr) {
    Node* p = pruneNode->parent;
    p->children.erase(std::remove(p->children.begin(), p->children.end(), pruneNode), p->children.end());
  }

  delete pruneNode;
}

void ScheduleTree::unregisterState(ExecutionState* state) {
  Node* n = getNodeOfExecutionState(state);
  if (n == nullptr) {
    return;
  }

  while (n->parent != nullptr && n->children.size() == 1) {
    n = n->parent;
  }

  delete(n);

  activeNodes.erase(state);
}

ScheduleTree::Node* ScheduleTree::registerNewChild(Node *base, ExecutionState *newState, ref<Expr> expr) {
  assert(base != nullptr && "Base node must be available");

  Node* newNode = new Node();
  newNode->parent = base;

  if (expr.isNull()) {
    newNode->type = SCHEDULING;

    Thread& curThread = newState->getCurrentThreadReference();
    newNode->scheduleIndex = base->scheduleIndex + 1;
    newNode->tid = curThread.getThreadId();
  } else {
    newNode->symbolicExpression = expr;
    newNode->type = SYMBOLIC;
  }

  base->children.push_back(newNode);
  activeNodes[newState] = newNode;

  return newNode;
}

void ScheduleTree::registerScheduleDecision(Node *base, std::vector<ExecutionState *>& newStates) {
  assert(!newStates.empty() && "There has to be a new state otherwise this does not make sense");

  base->children.reserve(newStates.size());

  for (auto state : newStates) {
    registerNewChild(base, state, nullptr);
  }
}

void ScheduleTree::registerSymbolicForks(Node *base, std::vector<ExecutionState *> &newStates,
                                         const std::vector<ref<Expr>> &expression) {
  assert(!newStates.empty() && "There has to be a new state otherwise this does not make sense");
  assert(newStates.size() == expression.size() && "States and expression should be equal");

  base->children.reserve(newStates.size());

  uint64_t i = 0;
  for (auto state : newStates) {
    registerNewChild(base, state, expression[i]);
    i++;
  }
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
  std::vector<ref<Expr>> constraints;

  availableHashes.insert(node->dependencyHash);
  availableHashes.insert(childToIgnore->dependencyHash);

  while (searchBase != nullptr) {
    // If there is only one child, then there is only
    if (searchBase->children.size() > 1) {
      bool found = hasEquivalentScheduleStep(searchBase, availableHashes, childToIgnore, stillNeeded, constraints);
      if (found) {
        return true;
      }
    }

    if (!searchBase->symbolicExpression.isNull()) {
      constraints.push_back(searchBase->symbolicExpression);
    }

    stillNeeded++;
    if (searchBase->dependencyHash != 0) {
      availableHashes.insert(searchBase->dependencyHash);
    }

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
  os << "\tnode [width=.1,height=.1,fontname=\"Terminus\"]\n";
  os << "\tedge [arrowsize=.5]\n";

  std::vector<Node*> stack;
  stack.push_back(root);

  while (!stack.empty()) {
    Node* n = stack.front();
    stack.erase(stack.begin());

    if (n->dependencyHash != 0) {
      os << "\tn" << n << "[label=\"" << (n->dependencyHash & 0xFFFF) << " [" << n->tid << "]\"];\n";
    } else {
      os << "\tn" << n << "[label=Scheduling];\n";
    }

    if (n->parent != nullptr) {
      os << "\tn" << n->parent << " -> n" << n << " [penwidth=2];\n";
    }

    if (n->dependencyHash != 0) {
      for (auto &dep : n->dependencies) {
        bool isMemory = (dep.reason & (1 | 2)) != 0;
        bool isOther = (dep.reason & (4 | 8 | 16)) != 0;

        if (isMemory) {
          os << "\tn" << n << " -> n" << dep.referencedNode << " [style=\"dashed\", color=gray];\n";
        }

        if (isOther) {
          os << "\tn" << n << " -> n" << dep.referencedNode << " [style=\"dashed\"];\n";
        }
      }
    }

    stack.insert(stack.end(), n->children.begin(), n->children.end());
  }
  os << "}\n";
}