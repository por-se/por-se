#include <list>

#include "PartialOrderExplorer.h"

using namespace klee;

static const Thread::ThreadId NO_RESULT = ~((uint64_t) 0);
static const ExecutionState::DependencyReason WEAK_DEPENDENCIES = ExecutionState::ATOMIC_MEMORY_ACCESS | ExecutionState::SAFE_MEMORY_ACCESS;

static uint64_t sForkCounter = 0;

PartialOrderExplorer::Node::~Node() {
  // TODO
}

void PartialOrderExplorer::Node::registerAnchoredRestriction(OrderingRelation *rel) {
  bool alreadyHasRestriction = false;

  for (auto r : anchoredRestrictions) {
    if (*r == *rel) {
      alreadyHasRestriction = true;
      break;
    }
  }

  if (!alreadyHasRestriction) {
    anchoredRestrictions.push_back(rel);
  }
}

PartialOrderExplorer::Path * PartialOrderExplorer::Path::splitPathAt(uint64_t scheduleIndex) {
  assert(scheduleIndex > root->scheduleIndex);
  assert(scheduleIndex <= root->scheduleIndex + scheduleHistory.size());

  uint64_t index = scheduleIndex - root->scheduleIndex;
  Node* firstOfNew = scheduleHistory[index];
  Path* newPath = new Path();

  newPath->counter = sForkCounter++;
  newPath->graph = graph;
  newPath->root = firstOfNew;
  newPath->scheduleHistory.assign(scheduleHistory.begin() + index, scheduleHistory.end());

  scheduleHistory.resize(index);

  std::vector<OrderingRelation*> rebalanceNew;
  restrictions.swap(rebalanceNew);

  // Either we can copy these again from our parent or they are all in the
  // new restrictions that we want to rebalance nevertheless
  allRestrictions.clear();

  if (parentMultiPath != nullptr) {
    // This is really save thing to do as we can just copy from them
    newPath->allRestrictions = parentMultiPath->parentPath->allRestrictions;
    allRestrictions = parentMultiPath->parentPath->allRestrictions;
  }

  for (Node* n : newPath->scheduleHistory) {
    n->path = newPath;
  }

  return newPath;
}

PartialOrderExplorer::Node* PartialOrderExplorer::Path::findPredecessor(Node* base) {
  // Since we want the predecessor and this info is encoded in the dependencies: use this
  for (auto& dep : base->dependencies) {
    if (dep.referencedNode->tid == base->tid && (dep.reason & ExecutionState::PREDECESSOR) != 0) {
      return dep.referencedNode;
    }
  }

  return nullptr;
}

PartialOrderExplorer::Node* PartialOrderExplorer::Path::getLastThreadExecution(Thread::ThreadId tid) {
  Node* current = scheduleHistory.back();

  while (current != nullptr) {
    if (current->dependencyHash != 0 && current->tid == tid) {
      return current;
    }

    current = current->parent;
  }

  return nullptr;
}

PartialOrderExplorer::Node* PartialOrderExplorer::Path::createNewNode() {
  Node* current = scheduleHistory.back();

  Node* newNode = new Node();
  newNode->path = current->path;
  newNode->parent = current;
  newNode->scheduleIndex = current->scheduleIndex + 1;
  scheduleHistory.push_back(newNode);

  return newNode;
}

void PartialOrderExplorer::Path::registerRestriction(OrderingRelation *rel, bool newOne) {
  // First of all sanity check that this will not violate any of our ones
  // or specifically: that it will not reverse any previously recorded one

  bool alreadyRegistered = false;
  for (auto r : allRestrictions) {
    assert(!r->isReverse(rel) && "We cannot both have the reverse and the non reverse");

    if (*r == *rel) {
      alreadyRegistered = true;
    }
  }

  if (alreadyRegistered) {
    return;
  }

  allRestrictions.push_back(rel);

  if (newOne) {
    restrictions.push_back(rel);
  }

  // And pass it done to the our children
  if (resultingMultiPath != nullptr) {
    for (auto c : resultingMultiPath->children) {
      c->registerRestriction(rel, false);
    }
  }
}

void PartialOrderExplorer::Path::registerEpochResult(ScheduleResult &result, ExecutionState *state) {
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
    Path* curBase = this;
    while (curBase->root->scheduleIndex > dep.scheduleIndex) {
      curBase = curBase->root->parent->path;
    }

    Node *reference = curBase->scheduleHistory[dep.scheduleIndex - curBase->root->scheduleIndex];
    assert(reference->scheduleIndex == dep.scheduleIndex && "Has to match");

    d.referencedNode = reference;
    current->dependencies.push_back(d);
  }
}

bool PartialOrderExplorer::Path::checkIfScheduleable(Thread::ThreadId tid, ExecutionState* state) {
  if (state->runnableThreads.find(tid) == state->runnableThreads.end()) {
    return false;
  }

  // So as a final step make sure that we satisfy our restrictions
  for (auto res : allRestrictions) {
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

void PartialOrderExplorer::Path::scheduleNextThread(ExecutionState *state, std::set<Thread::ThreadId> tids) {
  assert(!tids.empty() && "There has to be at least one thread that we can schedule");

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

  Node* pred = getLastThreadExecution(tid);
  for (auto res : restrictions) {
    if (tid == res->lowerTid && res->matchesLower(pred)) {
      node->registerAnchoredRestriction(res);
    }
  }

  // So we found a thread id. Setup everything
  node->tid = tid;
  if (lastNode->scheduleIndex < root->scheduleIndex) {
    return;
  }

  // In order to support forks we have to track all other threads that we can execute
  for (auto &t : tids) {
    if (t != tid) {
      lastNode->possibleOtherSchedules.insert(t);
    }
  }
}

bool PartialOrderExplorer::Path::checkIfPermutable(Node *dependency, Node *dependent) {
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

  for (auto res : allRestrictions) {
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

PartialOrderExplorer::MultiPath::MultiPath(PartialOrderExplorer::Path *parent, uint64_t splitAt) {
  MultiPath* pre = parent->resultingMultiPath;
  Path* split = parent->splitPathAt(splitAt);
  split->resultingMultiPath = pre;

  parentPath = parent;
  children.push_back(split);

  parentPath->resultingMultiPath = this;
  split->parentMultiPath = this;
}

PartialOrderExplorer::Path* PartialOrderExplorer::MultiPath::createNewPath() {
  Path* p = new Path();
  p->counter = sForkCounter++;
  p->parentMultiPath = this;
  p->allRestrictions = parentPath->allRestrictions;

  p->root = new Node();
  p->root->path = p;
  p->root->parent = parentPath->scheduleHistory.back();
  p->root->scheduleIndex = p->root->parent->scheduleIndex + 1;

  p->scheduleHistory.push_back(p->root);
  p->graph = parentPath->graph;

  children.push_back(p);

  return p;
}

PartialOrderExplorer::PartialOrderExplorer(ExecutionState *state, StateForkProvider &provider) {
  rootPath = new Path();
  rootPath->graph = this;

  rootPath->root = new Node();
  rootPath->root->tid = state->getCurrentThreadReference().getThreadId();
  rootPath->scheduleHistory.push_back(rootPath->root);
  rootPath->root->path = rootPath;

  responsiblePaths[state] = rootPath;
  responsiblePathsReverse[rootPath] = state;

  forkProvider = provider;
}

bool PartialOrderExplorer::mergeWithFork(ScheduleResult &result, Path *base, OrderingRelation *rel) {
  return true;
}

void PartialOrderExplorer::setupFork(ScheduleResult& result, Node* triggerNode, ScheduleDependency* dep) {
  Path* nodePath = triggerNode->path;

  // TODO: this memory we allocate here is never freed and as such leaked
  auto ordering = new OrderingRelation();
  ordering->lowerTid = triggerNode->tid;
  ordering->lowerPredNode = nodePath->findPredecessor(triggerNode);
  ordering->higherTid = dep->referencedNode->tid;
  ordering->higherPredNode = nodePath->findPredecessor(dep->referencedNode);

  Node* forkAt = dep->referencedNode->parent;
  if (forkAt->possibleOtherSchedules.empty()) {
    return;
  }

  MultiPath* mp = nullptr;
  Path* continuedPath = nullptr;

  if (dep->referencedNode == dep->referencedNode->path->root) {
    // So in this case we want to also check that we do not replicate a path
    mp = dep->referencedNode->path->parentMultiPath;

    for (Path* p : mp->children) {
      for (auto res : p->restrictions) {
        if (*res != *ordering) {
          continue;
        }

        // So we have a path were the same ordering already exists
        delete(ordering);
        return;
      }
    }

    continuedPath = nodePath;
  } else {
    mp = new MultiPath(forkAt->path, dep->referencedNode->scheduleIndex);
    assert(mp->children.size() == 1 && "Should be a clean multipath");
    assert(mp->parentPath->scheduleHistory.back() == forkAt && "Should have split just at forkAt");

    if (forkAt->path == nodePath) {
      continuedPath = mp->children[0];

      auto rIt = responsiblePathsReverse.find(forkAt->path);
      assert(rIt != responsiblePathsReverse.end());

      responsiblePaths[rIt->second] = continuedPath;
      responsiblePathsReverse[continuedPath] = rIt->second;
      responsiblePathsReverse.erase(rIt);
    } else {
      // So we fork at another layer, make sure that we do not change the whole thing to much
      continuedPath = dep->referencedNode->path;
    }
  }

  // Next step is to actually create the new path
  Path* newPath = mp->createNewPath();

  // And add all restrictions
  auto reverse = ordering->reverse();

  newPath->restrictions.push_back(ordering);
  continuedPath->registerRestriction(reverse, true);

  dep->referencedNode->registerAnchoredRestriction(reverse);

  newPath->forkReason = triggerNode;

  // Now try to schedule our new thread
  Node* reference = mp->parentPath->scheduleHistory.back();
  newPath->scheduleNextThread(reference->resultingState, reference->possibleOtherSchedules);
  Thread::ThreadId tid = newPath->root->tid;
  reference->possibleOtherSchedules.erase(tid);

  ExecutionState* state = nullptr;
  if (reference->possibleOtherSchedules.empty()) {
    state = reference->resultingState;
    reference->resultingState = nullptr;
    result.reactivatedStates.push_back(state);
  } else {
    state = forkProvider(reference->resultingState);
    result.newStates.push_back(state);
  }

  state->scheduleNextThread(tid);
  responsiblePaths[state] = newPath;
  responsiblePathsReverse[newPath] = state;
}

bool PartialOrderExplorer::checkIfIndependentFork(ScheduleResult& result, Node* triggerNode, ScheduleDependency* dep) {
  // So we want to know if we are in the main branch or a side branch.
  // So if we jump out of out fork than only for as long as we are on the side track

  if (dep->referencedNode->scheduleIndex < triggerNode->path->root->scheduleIndex) {
    return true;
  }

  // So check if when we jump out of our tree, if we are on the main track
  Path* curPath = triggerNode->path;
  while (curPath != nullptr && curPath->parentMultiPath != nullptr) {
    if (curPath->scheduleHistory.back()->scheduleIndex <= dep->referencedNode->parent->scheduleIndex) {
      break;
    }

    if (curPath != curPath->parentMultiPath->children.front()) {
      return false;
    }

    curPath = curPath->parentMultiPath->parentPath;
  }

  return true;
}

void PartialOrderExplorer::checkForNecessaryForks(ScheduleResult& result, Path* path) {
  Node* node = path->scheduleHistory.back();
  if (node->dependencies.empty()) {
    // So we did not add any dependencies so that means there is nothing to do
    return;
  }

  std::vector<ScheduleDependency*> forkCandidates {};
  for (auto& dep : node->dependencies) {
    bool isMemory = (dep.reason & (1 | 2)) != 0;

    if (isMemory) {
      forkCandidates.push_back(&dep);
    }
  }

  // So we want to fork for all of them if that is possible
  for (auto dep : forkCandidates) {
    // If we depend on another hard dependency that is fresher than our memory dependency,
    // then this memory dependency does not really influence this schedule
    if (!path->checkIfPermutable(dep->referencedNode, node)) {
      continue;
    }

    // So in general we want to disallow forks beyond our own tree.
    // (These would often create duplicates) so make sure that the resulting fork node is in our own tree
    if (dep->referencedNode->scheduleIndex + 1 < path->root->scheduleIndex) {
      continue;
    }

    if (!checkIfIndependentFork(result, node, dep)) {
      continue;
    }

    setupFork(result, node, dep);
  }
}

void PartialOrderExplorer::checkForNecessaryForksV2(ScheduleResult &result, Path *path) {
  Node* node = path->scheduleHistory.back();
  if (node->dependencies.empty()) {
    // So we did not add any dependencies so that means there is nothing to do
    return;
  }

  std::vector<ScheduleDependency*> forkCandidates {};
  for (auto& dep : node->dependencies) {
    bool isMemory = (dep.reason & (1 | 2)) != 0;

    if (isMemory && dep.referencedNode->parent != nullptr) {
      forkCandidates.push_back(&dep);
    }
  }

  // So we want to fork for all of them if that is possible
  for (auto dep : forkCandidates) {
    // If we depend on another hard dependency that is fresher than our memory dependency,
    // then this memory dependency does not really influence this schedule
    if (!path->checkIfPermutable(dep->referencedNode, node)) {
      continue;
    }

    // So in general we want to disallow forks beyond our own tree.
    // (These would often create duplicates) so make sure that the resulting fork node is in our own tree
    if (dep->referencedNode->scheduleIndex + 1 < path->root->scheduleIndex) {
      continue;
    }

    setupFork(result, node, dep);
    continue;

    // So here we want to decide which forking strategy we want to adopt
    Node* baseForkNode = dep->referencedNode->parent;

    // First of all we want to decide how far our scheduling decision is independent
    std::list<Node*> toCheck;
    std::list<Node*> beyond = {node};

    Path* independentUpTo = nullptr;
    bool sameAnchor = false;
    Path* curPath = path;

    while (curPath != nullptr && curPath->parentMultiPath != nullptr) {
      toCheck.swap(beyond);

      bool independent = true;

      while (!toCheck.empty()) {
        Node* n = toCheck.front();
        toCheck.pop_front();

        for (auto d : n->dependencies) {
          if (d.referencedNode->scheduleIndex < curPath->root->scheduleIndex) {
            beyond.push_back(d.referencedNode);
            continue;
          }

          if (!d.referencedNode->anchoredRestrictions.empty()) {
            independent = false;
            sameAnchor = d.referencedNode->parent == baseForkNode;
            break;
          }

          toCheck.push_back(d.referencedNode);
        }
      }

      if (!independent) {
        break;
      }

      independentUpTo = curPath;
      curPath = curPath->parentMultiPath->parentPath;
    }

    if (independentUpTo != nullptr || sameAnchor) {
      setupFork(result, node, dep);
    }
  }
}

PartialOrderExplorer::ScheduleResult PartialOrderExplorer::processEpochResult(ExecutionState *state) {
  ScheduleResult result;

  auto pathIt = responsiblePaths.find(state);
  assert(pathIt != responsiblePaths.end() && "There has to be a responsible tree");

  Path* path = pathIt->second;
  Node* readyNode = path->scheduleHistory.back();
  assert(readyNode->dependencyHash == 0 && "Node should not be processed before");

  // Step one is to register all results during the current schedule phase
  path->registerEpochResult(result, state);

  // So we recorded everything we needed, now save the resulting state if we can use this node
  // as a fork node ->  so basically only if we can fork for another thread
  if (state->runnableThreads.size() >= 2) {
    readyNode->resultingState = forkProvider(state);
    result.newInactiveStates.push_back(readyNode->resultingState);
  }

  checkForNecessaryForksV2(result, path);

  if (state->runnableThreads.empty()) {
    result.finishedState = state;
    return result;
  }

  // Now schedule the new thread, if we are not yet finished
  Node* newNode = readyNode->path->createNewNode();
  readyNode->path->scheduleNextThread(state, state->runnableThreads);
  state->scheduleNextThread(newNode->tid);

  return result;
}

void PartialOrderExplorer::dump(llvm::raw_ostream &os) {
  os << "digraph G {\n";
  os << "\tsize=\"10,7.5\";\n";
  os << "\tratio=fill;\n";
  os << "\tcenter = \"true\";\n";
  os << "\tnode [width=.1,height=.1,fontname=\"Terminus\"]\n";
  os << "\tedge [arrowsize=.5]\n";

  std::vector<Path*> stack;
  stack.push_back(rootPath);

  while (!stack.empty()) {
    Path* p = stack.front();
    stack.erase(stack.begin());
    std::string addInfo;

    for (auto n : p->scheduleHistory) {
      if (n->resultingState != nullptr) {
        addInfo += ", style=\"filled\"";
      }

      std::string i;
     for (auto r : n->anchoredRestrictions) {
       i += " " + std::to_string(r->lowerTid) + "<" + std::to_string(r->higherTid);
     }

      os << "\tn" << n << "[label=\"" << (n->dependencyHash & 0xFFFF) << " [" << n->tid << "]\n" << i << "\"" << addInfo << "];\n";

      if (p->parentMultiPath != nullptr && n == p->root) {
        std::string color = "green";

        if (p->parentMultiPath->children[0] == p) {
          color = "red";
        }

        std::string info = std::to_string(p->counter);

        for (auto r : p->restrictions) {
          info += "; " + std::to_string(r->lowerTid) + "<" + std::to_string(r->higherTid);
        }

        os << "\tm" << p->parentMultiPath << " -> n" << n << "[color=" << color << ",label=\"" << info << "\"]";

        if (p->forkReason != nullptr) {
          os << "\tn" << n << " -> n" << p->forkReason << "[style=dashed, color=green,constraint=false];\n";
        }
      } else if (n->parent != nullptr) {
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
    }

    if (p->resultingMultiPath == nullptr) {
      continue;
    }

    MultiPath* rm = p->resultingMultiPath;
    os << "\tm" << rm << "[label=\"T\", shape=box]";
    os << "\tn" << p->scheduleHistory.back() << " -> m" << rm << ";\n";

    for (auto c : rm->children) {
      stack.push_back(c);
    }
  }
  os << "}\n";
}