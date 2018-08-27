#include <list>

#include "PartialOrderGraph.h"

using namespace klee;

static const Thread::ThreadId NO_RESULT = ~((uint64_t) 0);
static const ExecutionState::DependencyReason WEAK_DEPENDENCIES = ExecutionState::ATOMIC_MEMORY_ACCESS | ExecutionState::SAFE_MEMORY_ACCESS;

static uint64_t sForkCounter = 0;

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
  root->tree = this;

  graph = parentTree->graph;

  // The root node will in this case be the first other scheduled thread
  root->parent = n;
  root->scheduleIndex = n->scheduleIndex + 1;
  scheduleHistory.push_back(root);
}

PartialOrderGraph::Tree::~Tree() {
  delete(root);

  scheduleHistory.clear();
}

PartialOrderGraph::Node* PartialOrderGraph::Tree::findPredecessor(Node* base) {
  // Since we want the predecessor and this info is encoded in the dependencies: use this
  for (auto& dep : base->dependencies) {
    if (dep.referencedNode->tid == base->tid && (dep.reason & ExecutionState::PREDECESSOR) != 0) {
      return dep.referencedNode;
    }
  }

  return nullptr;
}

PartialOrderGraph::Node* PartialOrderGraph::Tree::getLastThreadExecution(Thread::ThreadId tid) {
  Node* current = scheduleHistory.back();

  while (current != nullptr) {
    if (current->dependencyHash != 0 && current->tid == tid) {
      return current;
    }

    current = current->parent;
  }

  return nullptr;
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
    d.reason = dep.reason;

    // The scheduling history provides us with all the info that we need in order to find the node
    // efficiently
    Tree* curBase = this;
    while (curBase->root->scheduleIndex > dep.scheduleIndex) {
      curBase = curBase->parentTree;
    }

    Node *reference = curBase->scheduleHistory[dep.scheduleIndex - curBase->root->scheduleIndex];
    assert(reference->scheduleIndex == dep.scheduleIndex && "Has to match");

    d.referencedNode = reference;
    current->dependencies.push_back(d);
  }
}

bool PartialOrderGraph::Tree::checkIfScheduleable(Thread::ThreadId tid, ExecutionState* state) {
  if (state->runnableThreads.find(tid) == state->runnableThreads.end()) {
    return false;
  }

  // So as a final step make sure that we satisfy our restrictions
  for (auto res : restrictions) {
    if (res->higherTid != tid) {
      continue;
    }

    // So we have found an execution of a thread in our restrictions
    Node* pred = getLastThreadExecution(tid);
    if (!res->matchesHigher(pred)) {
      // Not the execution that we targeted
      continue;
    }

    // Now check if we satisfy the condition
    Node* lowerExecution = getLastThreadExecution(res->lowerTid);
    if (lowerExecution == nullptr) {
      return false;
    }

    // So one small part that we should actually really consider here: it can happen and is perfectly allowed
    // that we have executed the lower one more often before executing the current schedule so account for that as well
    Node* predLower = findPredecessor(lowerExecution);
    while (!res->matchesLower(predLower)) {
      if (predLower == nullptr) {
        return false;
      }

      predLower = findPredecessor(predLower);
    }
  }

  // So we found nothing that would prevent this
  return true;
}

void PartialOrderGraph::Tree::scheduleNextThread(ScheduleResult &result, ExecutionState *state, std::set<Thread::ThreadId> tids) {
  Node* node = scheduleHistory.back();
  Node* lastNode = node->parent;

  // There are different possible heuristics/rules on which thread to schedule
  Thread::ThreadId tid = NO_RESULT;

  if (tids.find(tid) != tids.end() && checkIfScheduleable(lastNode->tid, state)) {
    tid = lastNode->tid;
  }

  auto it = tids.begin();
  while (tid == NO_RESULT && it != tids.end()) {
    Thread::ThreadId t = *it;
    it++;

    if (checkIfScheduleable(t, state)) {
      tid = t;
      break;
    }
  }

  assert(tid != NO_RESULT);

//  if (!shadowSchedule.empty()) {
//    // So if we follow a shadow schedule then we want to rework this schedule as best as possible
//    // up to the destination node (forkTriggerNode);
//    Node* forkReasonNode = forkReason->dependency;
//    Node* forkTriggerNode = forkReason->dependent;
//
//    while (shadowScheduleIterator < shadowSchedule.size()) {
//      Node* shadowNode = shadowSchedule[shadowScheduleIterator];
//
//      if (shadowNode->tid == forkReasonNode->tid && shadowNode->scheduleIndex >= forkReasonNode->scheduleIndex) {
//        // we should definitely just skip this node, we want to execute the node
//        // after the actual fork trigger node
//        shadowScheduleIterator++;
//        continue;
//      }
//
//      Thread::ThreadId t = shadowNode->tid;
//      if (shadowNode == forkTriggerNode) {
//        assert(state->runnableThreads.find(t) != state->runnableThreads.end());
//
//        // Do not continue any further since we now try to schedule the thread that was previously dependent
//        shadowSchedule.clear();
//      }
//
//      if (state->runnableThreads.find(t) == state->runnableThreads.end()) {
//        // So this thread is not executable, skip it
//        shadowScheduleIterator++;
//        continue;
//      }
//
//      // When we are in a fork, then we do not want to track the alternatives for as long as we recreate the
//      // initial schedule
//      tid = t;
//      shadowScheduleIterator++;
//      break;
//    }
//  }
//
//  if (tid == NO_RESULT) {
//    // So we follow no shadow schedule. Our strategy now is to try to minimize the 'context switches'
//    // -> execute the same thread as long as possible
//    if (state->runnableThreads.find(lastNode->tid) != state->runnableThreads.end()) {
//      tid = lastNode->tid;
//    } else {
//      tid = *state->runnableThreads.begin();
//    }
//  }
//
//  // So as a final step make sure that we satisfy our restrictions
//  for (auto res : restrictions) {
//    if (res->higherTid != tid) {
//      continue;
//    }
//
//    // So we have found an execution of a thread in our restrictions
//    Node* pred = getLastThreadExecution(tid);
//    if (!res->matchesHigher(pred)) {
//      // Not the execution that we targeted
//      continue;
//    }
//
//    // Now check if we satisfy the condition
//    Node* lowerExecution = getLastThreadExecution(res->lowerTid);
//    assert(lowerExecution != nullptr && "We have to check the predecessor so this cannot be null if this schedule is valid");
//
//    // So one small part that we should actually really consider here: it can happen and is perfectly allowed
//    // that we have executed the lower one more often before executing the current schedule so account for that as well
//    Node* predLower = findPredecessor(lowerExecution);
//    while (!res->matchesLower(predLower)) {
//      assert(predLower != nullptr && "If we have null as predecessor then it should have matched");
//      predLower = findPredecessor(predLower);
//    }
//  }

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
PartialOrderGraph::Tree::activateScheduleFork(Node *triggerNode, ScheduleDependency *dep, ScheduleResult &result) {
  Node* forkAt = nullptr;

  // Next check is if we are actually independent from the previous fork. This means that we will reverse our
  // dependencies for as long as we do not find any other
  // By default we can go up to our tree and use that as a natural border
  // TODO: the algorithm below only accounts for one fork
  bool isIndependent = true;

  std::list<Node*> toCheck = { triggerNode };
  while (!toCheck.empty()) {
    Node* n = toCheck.front();
    toCheck.pop_front();

    for (auto d : n->dependencies) {
      if (d.referencedNode->scheduleIndex < root->scheduleIndex) {
        continue;
      }

      if (!d.referencedNode->anchoredRestrictions.empty()) {
        isIndependent = false;
      }

      toCheck.push_back(d.referencedNode);
    }
  }

  // So we try to find the previous anchor, if any and will use this as the `forkAt`
  if (isIndependent) {
    Node* n = dep->referencedNode->parent;
    for (; n != nullptr && n->parent != nullptr; n = n->parent) {
      if (n->anchoredRestrictions.empty()) {
        continue;
      }

      forkAt = n->parent;
      break;
    }
  }

  // Now the actual process:
  if (forkAt == nullptr) {
    forkAt = dep->referencedNode->parent;
  }

  assert(forkAt != nullptr && "We have to have a base in order to fork the tree");

  if (forkAt->resultingState == nullptr) {
    // So if we already scheduled all possible threads than abort
    return std::make_pair(nullptr, nullptr);
  }

  Tree* originTree = forkAt->tree;
  assert(originTree != nullptr && "We have to have a origin for the tree");

  // So first of all assemble the order that we want to enforce
  // TODO: this memory we allocate here is never freed and as such leaked
  auto ordering = new OrderingRelation();
  ordering->lowerTid = triggerNode->tid;
  ordering->lowerPredNode = findPredecessor(triggerNode);
  ordering->higherTid = dep->referencedNode->tid;
  ordering->higherPredNode = findPredecessor(dep->referencedNode);

  // Now check if this order is in any of the foreign trees
  for (auto ft : forkAt->foreignTrees) {
    // If we have already forked for this restriction then it will be given as a restriction in
    // the foreign tree, so try to find it there
    for (auto res : ft->restrictions) {
      if (res != ordering) {
        continue;
      }

      delete (ordering);
      return std::make_pair(nullptr, nullptr);
    }
  }

  // So we did not find this ordering in any of the other trees so start a new one
  Tree* fork = new Tree(forkAt, originTree);
  fork->independent = isIndependent;

  // Since we fork from here on, we want to register the reverse restriction to this tree
  // Note: this does not really change the scheduling, because we still have to check everything
  fork->restrictions.push_back(ordering);

  // And we need to add all that happened in the time span from [root, forkAt]
  Node* iterator = forkAt;
  Tree* depTree = iterator->tree;

  while (iterator != nullptr && iterator->scheduleIndex >= depTree->root->scheduleIndex) {
    fork->restrictions.insert(fork->restrictions.end(), iterator->anchoredRestrictions.begin(), iterator->anchoredRestrictions.end());
    iterator = iterator->parent;
  }

  // Correct since we have to note this here
  dep->referencedNode->anchoredRestrictions.push_back(ordering->reverse());

  // And copy the restrictions that we have from our parent, since we do not want to revert these
  fork->restrictions.insert(fork->restrictions.end(), depTree->restrictions.begin(), depTree->restrictions.end());

  // Now start the actual fork or more like try it
  fork->scheduleNextThread(result, forkAt->resultingState, forkAt->possibleOtherSchedules);

  // So we have a potential solution, we want to test that we did not already took it
  auto it = forkAt->possibleOtherSchedules.find(fork->root->tid);
  if (it == forkAt->possibleOtherSchedules.end()) {
    delete fork;
    return std::make_pair(nullptr, nullptr);
  }

  forkAt->foreignTrees.push_back(fork);
  fork->counter = ++sForkCounter;

  // So this is a valid fork. Test if we can actually have any more options left
  forkAt->possibleOtherSchedules.erase(it);

  // So we have a possible fork, then make sure that we actually will notify the others about this change

  ExecutionState* state = nullptr;
  if (forkAt->possibleOtherSchedules.empty()) {
    state = forkAt->resultingState;
    forkAt->resultingState = nullptr;
    result.reactivatedStates.push_back(state);
  } else {
    state = graph->forkProvider(forkAt->resultingState);
    result.newStates.push_back(state);
  }

  fork->forkTriggerNode = triggerNode;

  state->scheduleNextThread(fork->root->tid);

  Tree* base = originTree;
  while (base->parentTree != nullptr) {
    base->parentTree->activeLeafs++;
    base = base->parentTree;
  }

  originTree->activeLeafs++;

  return std::make_pair(fork, state);
}

//std::pair<PartialOrderGraph::Tree *, ExecutionState *>
//PartialOrderGraph::Tree::activateScheduleFork(Node *triggerNode, ScheduleDependency *dep, ScheduleResult &result) {
//  // So the basic semantic is: we want to fork from this thread and want to use
//  // the shadow schedule of the `base` tree
//
//  Node* forkAt = dep->referencedNode->parent;
//  assert(forkAt != nullptr && "We have to have a base in order to fork the tree");
//
//  if (forkAt->resultingState == nullptr) {
//    // So if we already scheduled all possible threads than abort
//    return std::make_pair(nullptr, nullptr);
//  }
//
//  Tree* fork = new Tree(forkAt, this);
//  fork->shadowSchedule.resize(triggerNode->scheduleIndex - forkAt->scheduleIndex, nullptr);
//
//  // Now add the shadow schedule
//  Node* n = triggerNode;
//  while (n != forkAt) {
//    fork->shadowSchedule[(n->scheduleIndex - forkAt->scheduleIndex) - 1] = n;
//    n = n->parent;
//  }
//
//  fork->forkReason = new PartialOrdering();
//  fork->forkReason->dependency = dep->referencedNode;
//  fork->forkReason->dependent = triggerNode;
//
//  // TODO: this memory we allocate here is never freed and as such leaked
//  auto ordering = new OrderingRelation();
//  ordering->lowerTid = triggerNode->tid;
//  ordering->lowerPredNode = findPredecessor(triggerNode);
//  ordering->higherTid = dep->referencedNode->tid;
//  ordering->higherPredNode = findPredecessor(dep->referencedNode);
//
//  // Since we fork from here on, we want to register the reverse restriction to this tree
//  // Note: this does not really change the scheduling, because we still have to check everything
//  fork->restrictions.push_back(ordering);
//
//  // And we need to add all that happened in the time span from [root, forkAt]
//  Node* iterator = forkAt;
//  Tree* depTree = iterator->tree;
//
//  while (iterator != nullptr && iterator->scheduleIndex >= depTree->root->scheduleIndex) {
//    fork->restrictions.insert(fork->restrictions.end(), iterator->anchoredRestrictions.begin(), iterator->anchoredRestrictions.end());
//    iterator = iterator->parent;
//  }
//
//  // Correct since we have to note this here
//  dep->referencedNode->anchoredRestrictions.push_back(ordering->reverse());
//
//  // And copy the restrictions that we have from our parent, since we do not want to revert these
//  fork->restrictions.insert(fork->restrictions.end(), depTree->restrictions.begin(), depTree->restrictions.end());
//
//  // TODO: if this restriction is independent from earlier restrictions, then
//  //       we do not want to add these to this base fork combo, but rather to an earlier one
//  // restrictions.push_back(ordering->reverse());
//
//  // Now start the actual fork or more like try it
//  fork->scheduleNextThread(result, forkAt->resultingState);
//
//  // So we have a potential solution, we want to test that we did not already took it
//  auto it = forkAt->possibleOtherSchedules.find(fork->root->tid);
//  if (it == forkAt->possibleOtherSchedules.end()) {
//    delete fork;
//    return std::make_pair(nullptr, nullptr);
//  }
//
//  forkAt->foreignTrees.push_back(fork);
//
//  // So this is a valid fork. Test if we can actually have any more options left
//  forkAt->possibleOtherSchedules.erase(it);
//
//  ExecutionState* state = nullptr;
//  if (forkAt->possibleOtherSchedules.empty()) {
//    state = forkAt->resultingState;
//    forkAt->resultingState = nullptr;
//    result.reactivatedStates.push_back(state);
//  } else {
//    state = graph->forkProvider(forkAt->resultingState);
//    result.newStates.push_back(state);
//  }
//
//  state->scheduleNextThread(fork->root->tid);
//
//  Tree* base = this;
//  while (base->parentTree != nullptr) {
//    base->parentTree->activeLeafs++;
//    base = base->parentTree;
//  }
//
//  activeLeafs++;
//
//  return std::make_pair(fork, state);
//}

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

      // So first of all filter out all references that are weak ones to the dependency from the dependent
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

  // Test if this will change any restrictions
  Node* preDependency = findPredecessor(dependency);
  Node* preDependent = findPredecessor(dependent);

  for (auto res : restrictions) {
    // What we may not do is reverse what we now already have done before
    if (dependency->tid != res->lowerTid) {
      continue;
    }

    if (dependent->tid != res->higherTid) {
      continue;
    }

    // Now we have to test if it matches the predecessors
    if (!res->matchesHigher(preDependent)) {
      continue;
    }

    if (!res->matchesLower(preDependency)) {
      continue;
    }

    // So we want to reverse something that we did before; abort
    return false;
  }

  return true;
}

std::vector<std::pair<PartialOrderGraph::Tree*, ExecutionState*>>
PartialOrderGraph::Tree::checkForNecessaryForks(ScheduleResult& result) {
  Node* node = scheduleHistory.back();

  if (node->dependencies.empty()) {
    // So we did not add any dependencies so that means there is nothing to do
    return std::vector<std::pair<Tree*, ExecutionState*>> {};
  }

  std::vector<ScheduleDependency*> forkCandidates {};
  for (auto& dep : node->dependencies) {
    bool isMemory = (dep.reason & (1 | 2)) != 0;

    if (isMemory) {
      forkCandidates.push_back(&dep);
    }
  }

  std::vector<std::pair<Tree*, ExecutionState*>> newForks {};
  std::vector<OrderingRelation*> newRestrictions {};

  // So we want to fork for all of them if that is possible
  for (auto dep : forkCandidates) {
    // If we depend on another hard dependency that is fresher than our memory dependency,
    // then this memory dependency does not really influence this schedule
    if (!checkIfPermutable(dep->referencedNode, node)) {
      continue;
    }

    // So in general we want to disallow forks beyond our own tree.
    // (These would often create duplicates) so make sure that the resulting fork node is in our own tree
    if (dep->referencedNode->scheduleIndex + 1 < root->scheduleIndex) {
      continue;
    }

    // Now try to trigger an alternative schedule
    auto f = activateScheduleFork(scheduleHistory.back(), dep, result);
    if (f.first != nullptr) {
      newForks.push_back(f);

      // Per definition the first entry is the new restriction
      newRestrictions.push_back(f.first->restrictions.at(0)->reverse());
    }
  }

  forwardRestrictions.insert(forwardRestrictions.end(), newRestrictions.begin(), newRestrictions.end());

  return newForks;
}

PartialOrderGraph::PartialOrderGraph(ExecutionState *state) {
  rootTree = new Tree();
  rootTree->root = new Node();
  rootTree->root->tree = rootTree;
  rootTree->root->tid = state->getCurrentThreadReference()->getThreadId();
  rootTree->scheduleHistory.push_back(rootTree->root);

  rootTree->graph = this;
  rootTree->activeLeafs++;

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

void PartialOrderGraph::cleanUpTree(Tree* base, ScheduleResult& result) {
  std::list<Node*> cleanup = { base->root };

  while (!cleanup.empty()) {
    Node* n = cleanup.front();
    cleanup.pop_front();

    if (n->resultingState != nullptr) {
      result.stoppedStates.push_back(n->resultingState);
      n->resultingState = nullptr;
    }

    if (n->directChild != nullptr) {
      cleanup.push_back(n->directChild);
    }

    for (auto ft : n->foreignTrees) {
      if (!ft->finished) {
        cleanup.push_back(ft->root);
      }
    }
  }
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
    // TODO: cleanup shadow copies of all threads that are no longer needed (by using the furthest back dependency)

    // This is what we can always cleanup
    Node* n = readyNode;
    while (n != nullptr && n->foreignTrees.empty() && false) {
      // Only clean up states that are actually there
      if (n->resultingState != nullptr) {
        result.stoppedStates.push_back(n->resultingState);
        n->resultingState = nullptr;
      }

      n = n->parent;
    }

    // Now register at all parent trees that we are now ready
    Tree* cleanUp = nullptr;
    Tree* current = tree;
    while (current != nullptr && false) {
      // So we found a tree that we forked from so set this up
      current->activeLeafs--;

      // So when we found a tree where no one is any longer active then we can use it as
      // a base for the cleanup
      if (current->activeLeafs == 0) {
        assert((cleanUp == nullptr || cleanUp->parentTree == current) && "We have to be going up without skipping one");
        cleanUp = current;
      }

      current = current->parentTree;
    }

    if (cleanUp != nullptr) {
      cleanUpTree(cleanUp, result);

      // We cleaned up this tree so it is finished by definition
      cleanUp->finished = true;
    }

    return result;
  }

  // Now schedule the new thread, if we are not yet finished
  Node* newNode = new Node();
  newNode->tree = readyNode->tree;
  newNode->parent = readyNode;
  readyNode->directChild = newNode;
  newNode->scheduleIndex = readyNode->scheduleIndex + 1;
  tree->scheduleHistory.push_back(newNode);

  tree->scheduleNextThread(result, state, state->runnableThreads);
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
      std::string c = t->independent ? "red" : "green";

      os << "\tn" << n << " -> n" << t->root << "[penwidth=2,color=green];\n";
      os << "\tn" << t->root << " -> n" << t->forkTriggerNode << "[label=\"" << t->counter << "\",style=dashed, color=" << c << ",constraint=false];\n";
      stack.push_back(t->root);
    }
  }
  os << "}\n";
}