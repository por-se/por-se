#include "PartialOrderGraph.h"

using namespace klee;

PartialOrderGraph::Tree::Tree(Node* n, Tree* parent) {
  isFork = true;
  forkedAtNode = n;
  parentTree = parent;

  root = new Node();
  root->parent = n;
  root->scheduleIndex = n->scheduleIndex + 1;
  scheduleHistory.push_back(root);
}

void PartialOrderGraph::Tree::registerEpochResult(ScheduleResult &result, ExecutionState *state) {
  // so the result is for our current node
  Node* current = scheduleHistory.back();

  // Step 1: extract all relevant info
  current->dependencyHash = state->schedulingHistory.back().dependencyHash;
  auto deps = state->getCurrentEpochDependencies();
  for (auto& dep : deps->dependencies) {
    if (dep.tid == current->tid) {
      // These don't interest us
      continue;
    }

    ScheduleDependency d {};
    d.scheduleIndex = dep.scheduleIndex;
    d.reason = dep.reason;

    // Use the scheduling history for as long as possible
    Node* trampoline = current->parent;
    //if (d.scheduleIndex >= root->scheduleIndex) {
    //  trampoline = scheduleHistory[d.scheduleIndex - root->scheduleIndex];
    //} else {
    //  trampoline = root;
    //}

    while (trampoline->scheduleIndex != d.scheduleIndex) {
      trampoline = trampoline->parent;
    }

    d.referencedNode = trampoline;
    current->dependencies.push_back(d);
  }

  // Step 2: build the next node
  Node* next = new Node();

  if (state->runnableThreads.empty()) {
    delete(next);

    result.finishedState = state;
    return;
  } else {
    auto it = state->runnableThreads.find(current->tid);
    if (it != state->runnableThreads.end()) {
      next->tid = current->tid;

      for (auto tid : state->runnableThreads) {
        if (tid == next->tid) {
          continue;
        }

        current->possibleOtherSchedules.insert(tid);
      }
    } else {
      it = state->runnableThreads.begin();
      next->tid = *it;
      ++it;

      current->possibleOtherSchedules.insert(it, state->runnableThreads.end());
    }
  }

  next->parent = current;
  next->scheduleIndex = current->scheduleIndex + 1;
  current->directChild = next;

  // Now prepare all our state saving
  // next->pausedState = state->branch();
  current->pausedState = state->branch();
  result.newInactiveStates.push_back(current->pausedState);

  // Set up the actual thread scheduling
  state->scheduleNextThread(next->tid);

  scheduleHistory.push_back(next);
}

PartialOrderGraph::PartialOrderGraph(ExecutionState *state) {
  rootTree = new Tree();
  rootTree->root = new Node();
  rootTree->root->tid = state->getCurrentThreadReference()->getThreadId();

  rootTree->scheduleHistory.push_back(rootTree->root);

  responsibleTrees[state] = rootTree;
}

PartialOrderGraph::ScheduleResult PartialOrderGraph::registerEpochResult(ExecutionState *state) {
  ScheduleResult result;

  // Phase 1: find the corresponding tree
  auto treeIt = responsibleTrees.find(state);
  assert(treeIt != responsibleTrees.end());

  // Now pass every information down to this tree
  Tree* workingTree = treeIt->second;
  workingTree->registerEpochResult(result, state);

  // Phase 2: find necessary forks and add them
  assert(workingTree->scheduleHistory.size() >= 2 && "At this point we have to have at least two nodes in this tree");

  Node* processedNode = *(workingTree->scheduleHistory.rbegin() + 1);
  if (processedNode->dependencies.empty()) {
    // So if there is actually no new dependency then we cannot do anything
    return result;
  }

  // So now try to find the furthest weak (aka memory dependency) that we can try to rewire
  Node* weakDependencyTarget = nullptr;
  uint64_t scheduleIndex = processedNode->scheduleIndex;
  uint64_t lowestScheduleIndex = workingTree->root->scheduleIndex;

  for (auto& dep : processedNode->dependencies) {
    if (dep.scheduleIndex < scheduleIndex && dep.reason & 1 && dep.scheduleIndex >= lowestScheduleIndex) {
      weakDependencyTarget = dep.referencedNode;
      scheduleIndex = dep.scheduleIndex;
    }
  }

  if (weakDependencyTarget == nullptr) {
    // Again not much we can do
    return result;
  }

  // So now we want to move as far back as we can from this referenced node
  // where we have a potential fork

  Node* potentialFork = weakDependencyTarget;
  while (potentialFork->parent != nullptr) {
    potentialFork = potentialFork->parent;

    if (potentialFork->possibleOtherSchedules.find(processedNode->tid) != potentialFork->possibleOtherSchedules.end()) {
      break;
    }
  }

  // So filter out when we would jump out of our tree
  if (potentialFork->scheduleIndex < lowestScheduleIndex) {
    return result;
  }

  // TODO: can we prune everything between [potentialFork, ..., processedNode] ?
  Node* n = processedNode->parent;
  while (n != potentialFork) {
    if (n->possibleOtherSchedules.find(processedNode->tid) != n->possibleOtherSchedules.end()) {
      n->possibleOtherSchedules.erase(processedNode->tid);
    }

    n = n->parent;
  }

  ExecutionState* newState = nullptr;
  if (potentialFork->possibleOtherSchedules.size() == 1) {
    newState = potentialFork->pausedState;
    result.reactivatedStates.push_back(newState);
    potentialFork->pausedState = nullptr;
    potentialFork->possibleOtherSchedules.clear();
  } else {
    newState = potentialFork->pausedState->branch();
    result.newStates.push_back(newState);
    potentialFork->possibleOtherSchedules.erase(processedNode->tid);
  }

  // Create a new tree at this base node
  Tree* fork = new Tree(potentialFork, workingTree);
  responsibleTrees[newState] = fork;
  fork->root->tid = processedNode->tid;
  newState->scheduleNextThread(fork->root->tid);

  // And add the information why we forked
  fork->forkTriggerNode = processedNode;
  potentialFork->foreignTrees.push_back(fork);

  return result;
}

void PartialOrderGraph::dump(llvm::raw_ostream &os) {
  os << "digraph G {\n";
  os << "\tsize=\"10,7.5\";\n";
  os << "\tratio=fill;\n";
  os << "\tcenter = \"true\";\n";
  os << "\tnode [width=.1,height=.1,fontname=\"Terminus\"]\n";
  os << "\tedge [arrowsize=.5]\n";

  std::vector<Node*> stack;
  stack.push_back(rootTree->root);

  while (!stack.empty()) {
    Node* n = stack.front();
    stack.erase(stack.begin());

    os << "\tn" << n << "[label=\"" << (n->dependencyHash & 0xFFFF) << " [" << n->tid << "]\"];\n";
    if (n->parent != nullptr) {
      os << "\tn" << n->parent << " -> n" << n << " [penwidth=2];\n";
    }

    for (auto tid : n->possibleOtherSchedules) {
      os << "\tn" << n << "_" << tid << " [label=\"" << tid << "\", color=gray];\n";
      os << "\tn" << n << "-> n" << n << "_" << tid << " [style=dashed, color=gray]\n";
    }

    for (auto& dep : n->dependencies) {
      bool isMemory = (dep.reason & 1) == 1;
      bool isOther = (dep.reason & (2 | 4 | 8)) != 0;

      if (isMemory && !isOther) {
        os << "\tn" << n << " -> n" << dep.referencedNode << " [style=\"dashed\", color=gray];\n";
      } else {
        os << "\tn" << n << " -> n" << dep.referencedNode << " [style=\"dashed\"];\n";
      }
    }

    if (n->directChild != nullptr) {
      stack.push_back(n->directChild);
    }

    for (auto t : n->foreignTrees) {
      os << "\tn" << n << " -> n" << t->root << "[penwidth=2,color=green];\n";
      os << "\tn" << t->root << " -> n" << t->forkTriggerNode << "[style=dashed, color=green,constraint=false];\n";
      stack.push_back(t->root);
    }
  }
  os << "}\n";
}