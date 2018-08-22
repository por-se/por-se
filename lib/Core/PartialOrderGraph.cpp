#include <list>

#include "PartialOrderGraph.h"

using namespace klee;

static const Thread::ThreadId NO_RESULT = ~((uint64_t) 0);
static const ExecutionState::DependencyReason WEAK_DEPENDENCIES = ExecutionState::ATOMIC_MEMORY_ACCESS | ExecutionState::SAFE_MEMORY_ACCESS;

static ExecutionState* DEFAULT_PROVIDER(ExecutionState* st) {
  return st->branch();
}

PartialOrderGraph::Node::~Node() {
  for (auto ft : foreignTrees) {
    delete(ft);
  }

  foreignTrees.clear();
  delete(directChild);

  resultingState = nullptr;
}

PartialOrderGraph::Tree::Tree(Node* n, Tree* parent) {
  // This is the forking constructor
  parentTree = parent;
  root = new Node();

  graph = parentTree->graph;

  // The root node will in this case be the first other scheduled thread
  root->parent = n;
  root->scheduleIndex = n->scheduleIndex + 1;
  scheduleHistory.push_back(root);
}

PartialOrderGraph::Tree::~Tree() {
  delete(root);
  delete(forkReason);

  scheduleHistory.clear();
}

void PartialOrderGraph::Tree::registerEpochResult(ScheduleResult &result, ExecutionState *state) {
  // so the result is for our current node
  Node *current = scheduleHistory.back();

  // Step 1: extract all relevant info
  current->dependencyHash = state->schedulingHistory.back().dependencyHash;

  auto deps = state->getCurrentEpochDependencies();
  for (auto &dep : deps->dependencies) {
    if (dep.tid == current->tid) {
      // These don't interest us, so we ignore them
      continue;
    }

    ScheduleDependency d{};
    d.scheduleIndex = dep.scheduleIndex;
    d.reason = dep.reason;

    // Use the scheduling history for as long as possible
    Node *trampoline;
    if (d.scheduleIndex >= root->scheduleIndex) {
      trampoline = scheduleHistory[d.scheduleIndex - root->scheduleIndex];
    } else {
      trampoline = root;
    }

    while (trampoline->scheduleIndex != d.scheduleIndex) {
      trampoline = trampoline->parent;
    }

    d.referencedNode = trampoline;
    current->dependencies.push_back(d);
  }
}

void PartialOrderGraph::Tree::scheduleNextThread(ScheduleResult &result, ExecutionState *state) {
  Node* node = scheduleHistory.back();
  Node* lastNode = node->parent;

  // There are different possible heuristics/rules on which thread to schedule
  Thread::ThreadId tid = NO_RESULT;

  if (!shadowSchedule.empty()) {
    // So if we follow a shadow schedule then we want to rework this schedule as best as possible
    // up to the destination node (forkTriggerNode);
    Node* forkReasonNode = forkReason->dependency;
    Node* forkTriggerNode = forkReason->dependent;

    while (shadowScheduleIterator < shadowSchedule.size()) {
      Node* shadowNode = shadowSchedule[shadowScheduleIterator];

      if (shadowNode->tid == forkReasonNode->tid && shadowNode->scheduleIndex >= forkReasonNode->scheduleIndex) {
        // we should definitely just skip this node, we want to execute the node
        // after the actual fork trigger node
        shadowScheduleIterator++;
        continue;
      }

      Thread::ThreadId t = shadowNode->tid;
      if (shadowNode == forkTriggerNode) {
        assert(state->runnableThreads.find(t) != state->runnableThreads.end());

        changedNode = node;

        // Do not continue any further since we now try to schedule the thread that was previously dependent
        shadowSchedule.clear();
      }

      if (state->runnableThreads.find(t) == state->runnableThreads.end()) {
        // So this thread is not executable, skip it
        shadowScheduleIterator++;
        continue;
      }

      // When we are in a fork, then we do not want to track the alternatives for as long as we recreate the
      // initial schedule
      tid = t;
      shadowScheduleIterator++;
      break;
    }
  }

  if (tid == NO_RESULT) {
    // So we follow no shadow schedule. Our strategy now is to try to minimize the 'context switches'
    // -> execute the same thread as long as possible
    if (state->runnableThreads.find(lastNode->tid) != state->runnableThreads.end()) {
      tid = lastNode->tid;
    } else {
      tid = *state->runnableThreads.begin();
    }
  }

  // So we found a thread id. Setup everything
  node->tid = tid;
  if (lastNode->scheduleIndex < root->scheduleIndex) {
    return;
  }

  // In order to support forks we have to track all other threads that we can execute
  for (auto &t : state->runnableThreads) {
    if (t != tid) {
      lastNode->possibleOtherSchedules.insert(t);
    }
  }
}

std::pair<PartialOrderGraph::Tree*, ExecutionState*>
PartialOrderGraph::Tree::activateScheduleFork(Tree* base, Node *triggerNode, ScheduleDependency *dep, ScheduleResult &result) {
  // So the basic semantic is: we want to fork from this thread and want to use
  // the shadow schedule of the `base` tree

  Node* forkAt = dep->referencedNode->parent;

  assert(forkAt != nullptr && "We have to have a base in order to fork the tree");

  if (forkAt->resultingState == nullptr) {
    // So if we already scheduled all possible threads than abort
    return std::make_pair(nullptr, nullptr);
  }

  Tree* fork = new Tree(forkAt, base);
  fork->shadowSchedule.resize(triggerNode->scheduleIndex - forkAt->scheduleIndex, nullptr);

  // Now add the shadow schedule
  Node* n = triggerNode;
  while (n != forkAt) {
    fork->shadowSchedule[(n->scheduleIndex - forkAt->scheduleIndex) - 1] = n;
    n = n->parent;
  }

  fork->forkReason = new PartialOrdering();
  fork->forkReason->dependency = dep->referencedNode;
  fork->forkReason->dependent = triggerNode;

  // Now start the actual fork or more like try it
  fork->scheduleNextThread(result, forkAt->resultingState);

  // So we have a potential solution, we want to test that we did not already took it
  auto it = forkAt->possibleOtherSchedules.find(fork->root->tid);
  if (it == forkAt->possibleOtherSchedules.end()) {
    delete fork;
    return std::make_pair(nullptr, nullptr);
  }

  forkAt->foreignTrees.push_back(fork);

  // So this is a valid fork. Test if we can actually have any more options left
  forkAt->possibleOtherSchedules.erase(it);

  ExecutionState* state = nullptr;
  if (forkAt->possibleOtherSchedules.empty()) {
    state = forkAt->resultingState;
    forkAt->resultingState = nullptr;
    result.reactivatedStates.push_back(state);
  } else {
    state = graph->forkProvider(forkAt->resultingState);
    result.newStates.push_back(state);
  }

  state->scheduleNextThread(fork->root->tid);

  return std::make_pair(fork, state);
}

bool PartialOrderGraph::Tree::checkIfPermutable(Node *dependency, Node *dependent) {
  // So first of all we should check if we can actually change the order
  std::list<Node*> stillToCheck = { dependent };

  // So our checks should not go out of the current tree and should not
  // go beyond the `dependency`
  while (!stillToCheck.empty()) {
    Node* n = stillToCheck.front();
    stillToCheck.pop_front();

    // So if this node that we depend on is actually to either our 'weak' dependency (or a later execution
    // of the same thread), then we cannot change the scheduling
    if (n->tid == dependency->tid && n->scheduleIndex >= dependency->scheduleIndex) {
      return false;
    }

    for (auto& dep : n->dependencies) {
      uint8_t filteredReasons = dep.reason & ~WEAK_DEPENDENCIES;

      // So first of all filter out all references that are week ones to the dependency from the dependent
      if (n == dependent && dep.referencedNode == dependency && filteredReasons == 0) {
        continue;
      }

      // So this dependency is before our current analysis window; abort
      if (dep.referencedNode->scheduleIndex < dependency->scheduleIndex) {
        continue;
      }

      stillToCheck.push_back(dep.referencedNode);
    }
  }

  // So we actually can put it before the `dependency` node. So make sure that we do not duplicate efforts
  // that we did in another fork
  if (parentTree == nullptr) {
    // We are actually no fork so we cannot revert and changes in the fork
    return true;
  }

  // If we change the already changed node again based on the previously executed change then
  // we would just recreate the previous situation
  return !(changedNode != nullptr && changedNode == dependency && dependent->tid == forkReason->dependency->tid);
}

std::vector<std::pair<PartialOrderGraph::Tree*, ExecutionState*>>
PartialOrderGraph::Tree::checkForNecessaryForks(ScheduleResult& result) {
  Node* node = scheduleHistory.back();

  if (node->dependencies.empty()) {
    // So we did not add any dependencies so that means there is nothing to do
    return std::vector<std::pair<Tree*, ExecutionState*>> {};
  }

  std::vector<ScheduleDependency*> forkCandidates;
  for (auto& dep : node->dependencies) {
    bool isMemory = (dep.reason & (1 | 2)) != 0;

    if (isMemory) {
      forkCandidates.push_back(&dep);
    }
  }

  std::vector<std::pair<Tree*, ExecutionState*>> newForks;

  // So we want to fork for all of them if that is possible
  for (auto dep : forkCandidates) {
    // If we depend on another hard dependency that is fresher than our memory dependency,
    // then this memory dependency does not really influence this schedule
    if (!checkIfPermutable(dep->referencedNode, node)) {
      continue;
    }

    // So in general we want to disallow forks beyond our own tree.
    // (These would often create duplicates) so make sure that the resulting fork node is in our own tree
    if (dep->scheduleIndex + 1 < root->scheduleIndex) {
      continue;
    }

    // Now try to trigger an alternative schedule
    auto f = activateScheduleFork(this, scheduleHistory.back(), dep, result);
    if (f.first != nullptr) {
      newForks.push_back(f);
    }
  }

  return newForks;
}

PartialOrderGraph::PartialOrderGraph(ExecutionState *state) {
  rootTree = new Tree();
  rootTree->root = new Node();
  rootTree->root->tid = state->getCurrentThreadReference()->getThreadId();
  rootTree->scheduleHistory.push_back(rootTree->root);

  rootTree->graph = this;

  responsibleTrees[state] = rootTree;
  forkProvider = DEFAULT_PROVIDER;
}

PartialOrderGraph::PartialOrderGraph(ExecutionState *state, StateForkProvider &provider) : PartialOrderGraph(state) {
  forkProvider = provider;
}

PartialOrderGraph::~PartialOrderGraph() {
  delete(rootTree);
  responsibleTrees.clear();
}

PartialOrderGraph::ScheduleResult PartialOrderGraph::processEpochResult(ExecutionState *state) {
  ScheduleResult result;

  auto treeIt = responsibleTrees.find(state);
  assert(treeIt != responsibleTrees.end() && "There has to be a responsible tree");

  Tree* tree = treeIt->second;
  Node* readyNode = tree->scheduleHistory.back();
  assert(readyNode->dependencyHash == 0 && "Node should not be processed before");

  // Step one is to register all results during the current schedule phase
  tree->registerEpochResult(result, state);

  // So we recorded everything we needed, now save the resulting state if we can use this node
  // as a fork node ->  so basically only if we can fork for another thread
  if (state->runnableThreads.size() >= 2) {
    readyNode->resultingState = forkProvider(state);
    result.newInactiveStates.push_back(readyNode->resultingState);
  }

  std::vector<std::pair<Tree*, ExecutionState*>> newForks = tree->checkForNecessaryForks(result);
  for (auto f : newForks) {
    responsibleTrees[f.second] = f.first;
  }

  if (state->runnableThreads.empty()) {
    result.finishedState = state;
    // TODO: cleanup shadow copies of all threads that are no longer needed

    return result;
  }

  // Now schedule the new thread, if we are not yet finished
  Node* newNode = new Node();
  newNode->parent = readyNode;
  readyNode->directChild = newNode;
  newNode->scheduleIndex = readyNode->scheduleIndex + 1;
  tree->scheduleHistory.push_back(newNode);

  tree->scheduleNextThread(result, state);
  state->scheduleNextThread(newNode->tid);

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
    std::string addInfo;

    if (n->resultingState != nullptr) {
      addInfo += ", style=\"filled\"";
    }

    os << "\tn" << n << "[label=\"" << (n->dependencyHash & 0xFFFF) << " [" << n->tid << "]\"" << addInfo << "];\n";

    if (n->parent != nullptr) {
      os << "\tn" << n->parent << " -> n" << n << " [penwidth=2];\n";
    }

    for (auto tid : n->possibleOtherSchedules) {
      os << "\tn" << n << "_" << tid << " [label=\"" << tid << "\", color=gray];\n";
      os << "\tn" << n << "-> n" << n << "_" << tid << " [style=dashed, color=gray]\n";
    }

    for (auto& dep : n->dependencies) {
      bool isMemory = (dep.reason & (1 | 2)) != 0;
      bool isOther = (dep.reason & (4 | 8 | 16)) != 0;

      if (isMemory) {
        os << "\tn" << n << " -> n" << dep.referencedNode << " [style=\"dashed\", color=gray];\n";
      }

      if (isOther) {
        os << "\tn" << n << " -> n" << dep.referencedNode << " [style=\"dashed\"];\n";
      }
    }

    if (n->directChild != nullptr) {
      stack.push_back(n->directChild);
    }

    for (auto t : n->foreignTrees) {
      os << "\tn" << n << " -> n" << t->root << "[penwidth=2,color=green];\n";
      os << "\tn" << t->root << " -> n" << t->forkReason->dependent << "[style=dashed, color=green,constraint=false];\n";
      stack.push_back(t->root);
    }
  }
  os << "}\n";
}