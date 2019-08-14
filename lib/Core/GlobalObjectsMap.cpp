#include "GlobalObjectsMap.h"
#include "MemoryManager.h"



using namespace klee;

//
// GlobalObjectReference
//

GlobalObjectsMap::GlobalObjectReference::GlobalObjectReference(const llvm::Function* f, ref<ConstantExpr> addr)
        : type(RefFunction), value(f), address(addr) {}

GlobalObjectsMap::GlobalObjectReference::GlobalObjectReference(const llvm::GlobalAlias* a, ref<ConstantExpr> addr)
        : type(RefAlias), value(a), address(addr) {}

GlobalObjectsMap::GlobalObjectReference::GlobalObjectReference(const llvm::GlobalValue* v, std::size_t size)
        : type(RefData), value(v), address(0), size(size) {}

const llvm::Function* GlobalObjectsMap::GlobalObjectReference::getFunction() {
  assert(type == RefFunction && "Calling on invalid type");
  return static_cast<const llvm::Function*>(value);
}

const llvm::GlobalAlias* GlobalObjectsMap::GlobalObjectReference::getAlias() {
  assert(type == RefAlias && "Calling on invalid type");
  return static_cast<const llvm::GlobalAlias*>(value);
}

const llvm::GlobalValue* GlobalObjectsMap::GlobalObjectReference::getGlobalValue() {
  assert(type == RefData && "Calling on invalid type");
  return static_cast<const llvm::GlobalValue*>(value);
}

MemoryObject* GlobalObjectsMap::GlobalObjectReference::getMemoryObject(const ThreadId& tid) {
  assert(type == RefData && "Calling on invalid type");

  auto it = threadLocalMemory.find(tid);
  if (it != threadLocalMemory.end()) {
    return it->second;
  }

  return nullptr;
}

//
// GlobalObjectsMap
//

GlobalObjectsMap::GlobalObjectsMap(MemoryManager* manager)
  : memoryManager(manager) {
}

void GlobalObjectsMap::registerFunction(const llvm::Function* func, ref<ConstantExpr> addr) {
  assert(findObject(func) == nullptr);

  globalObjects.insert(std::make_pair(func, GlobalObjectReference(func, addr)));
}

void GlobalObjectsMap::registerAlias(const llvm::GlobalAlias* alias, ref<ConstantExpr> addr) {
  assert(findObject(alias) == nullptr);

  globalObjects.insert(std::make_pair(alias, GlobalObjectReference(alias, addr)));
}

const MemoryObject* GlobalObjectsMap::registerGlobalData(const llvm::GlobalValue* gv, std::size_t size) {
  assert(findObject(gv) == nullptr);

  GlobalObjectReference reference(gv, size);

  // For the main thread we create the memory object directly
  ThreadId mainThreadId(1);

  auto mo = memoryManager->allocateGlobal(size, gv, mainThreadId, gv->getAlignment());
  reference.threadLocalMemory.insert(std::make_pair(mainThreadId, mo));

  if (!gv->isThreadLocal() && mo != nullptr) {
    reference.address = mo->getBaseExpr();
  }

  globalObjects.insert(std::make_pair(gv, reference));

  return mo;
}

const MemoryObject* GlobalObjectsMap::lookupGlobalMemoryObject(const llvm::GlobalValue* gv, const ThreadId& byTid) {
  auto globalObject = findObject(gv);

  if (globalObject == nullptr) {
    return nullptr;
  }

  assert(globalObject->type == RefData);

  // Now we have to check whether the value is actually depending on the thread
  // that is calling
  if (!gv->isThreadLocal()) {
    return globalObject->threadLocalMemory[ThreadId(1)];
  }

  // Now we have to check if we actually have already created the object for this specific thread
  auto it = globalObject->threadLocalMemory.find(byTid);
  if (it != globalObject->threadLocalMemory.end()) {
    return it->second;
  }

  auto mo = memoryManager->allocateGlobal(globalObject->size, gv, byTid, gv->getAlignment());
  globalObject->threadLocalMemory.insert(std::make_pair(byTid, mo));
  return mo;
}

ref<ConstantExpr> GlobalObjectsMap::lookupGlobal(const llvm::GlobalValue* gv, const ThreadId& byTid) {
  auto globalObject = findObject(gv);

  if (globalObject == nullptr) {
    return Expr::createPointer(0);
  }

  // All other types do not change based on the calling thread
  if (globalObject->type != RefData || !gv->isThreadLocal()) {
    return globalObject->address;
  }

  return lookupGlobalMemoryObject(gv, byTid)->getBaseExpr();
}

GlobalObjectsMap::GlobalObjectReference* GlobalObjectsMap::findObject(const llvm::GlobalValue* gv) {
  auto it = globalObjects.find(gv);
  if (it != globalObjects.end()) {
    return &it->second;
  }

  return nullptr;
}