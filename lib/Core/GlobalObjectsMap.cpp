#include "GlobalObjectsMap.h"

#include "MemoryManager.h"

#include "klee/ExecutionState.h"

using namespace klee;

//
// GlobalObjectReference
//

GlobalObjectsMap::GlobalObjectReference::GlobalObjectReference(
    const llvm::Function *f, ref<ConstantExpr> addr)
    : type(ReferencedType::Function), value(f), address(std::move(addr)), size(0) {}

GlobalObjectsMap::GlobalObjectReference::GlobalObjectReference(
    const llvm::GlobalAlias *a, ref<ConstantExpr> addr)
    : type(ReferencedType::Alias), value(a), address(std::move(addr)), size(0) {}

GlobalObjectsMap::GlobalObjectReference::GlobalObjectReference(
    const llvm::GlobalVariable *v, std::size_t size)
    : type(ReferencedType::Data), value(v), address(0), size(size) {}

const llvm::Function *GlobalObjectsMap::GlobalObjectReference::getFunction() {
  assert(type == ReferencedType::Function && "Calling on invalid type");
  return static_cast<const llvm::Function *>(value);
}

const llvm::GlobalAlias *GlobalObjectsMap::GlobalObjectReference::getAlias() {
  assert(type == ReferencedType::Alias && "Calling on invalid type");
  return static_cast<const llvm::GlobalAlias *>(value);
}

const llvm::GlobalVariable *
GlobalObjectsMap::GlobalObjectReference::getGlobalVariable() {
  assert(type == ReferencedType::Data && "Calling on invalid type");
  return static_cast<const llvm::GlobalVariable *>(value);
}

MemoryObject *
GlobalObjectsMap::GlobalObjectReference::getMemoryObject(const ThreadId &tid) {
  assert(type == ReferencedType::Data && "Calling on invalid type");

  auto it = threadLocalMemory.find(tid);
  if (it != threadLocalMemory.end()) {
    return it->second.get();
  }

  return nullptr;
}

//
// GlobalObjectsMap
//

void GlobalObjectsMap::registerFunction(const llvm::Function *func,
                                        ref<ConstantExpr> addr) {
  assert(findObject(func) == nullptr);

  globalObjects.emplace(func, GlobalObjectReference(func, std::move(addr)));
}

void GlobalObjectsMap::registerAlias(const llvm::GlobalAlias *alias,
                                     ref<ConstantExpr> addr) {
  assert(findObject(alias) == nullptr);

  globalObjects.emplace(alias, GlobalObjectReference(alias, std::move(addr)));
}

const MemoryObject *
GlobalObjectsMap::registerGlobalData(MemoryManager *manager,
                                     const llvm::GlobalVariable *gv,
                                     std::size_t size, std::size_t alignment) {
  assert(findObject(gv) == nullptr);

  GlobalObjectReference reference(gv, size);

  // For the main thread we create the memory object directly
  auto mo = manager->allocateGlobal(size, gv, ExecutionState::mainThreadId,
                                    alignment);
  reference.threadLocalMemory.emplace(ExecutionState::mainThreadId, mo);

  if (!gv->isThreadLocal() && mo != nullptr) {
    reference.address = mo->getBaseExpr();
  }

  globalObjects.emplace(gv, std::move(reference));

  return mo;
}

const MemoryObject *
GlobalObjectsMap::lookupGlobalMemoryObject(MemoryManager *manager,
                                           const llvm::GlobalVariable *gv,
                                           const ThreadId &byTid) {
  auto globalObject = findObject(gv);

  if (globalObject == nullptr) {
    return nullptr;
  }

  assert(globalObject->type == ReferencedType::Data);

  // Now we have to check whether the value is actually depending on the thread
  // that is calling
  if (!gv->isThreadLocal()) {
    return globalObject->threadLocalMemory[ExecutionState::mainThreadId].get();
  }

  // Now we have to check if we actually have already created the object for
  // this specific thread
  auto it = globalObject->threadLocalMemory.find(byTid);
  if (it != globalObject->threadLocalMemory.end()) {
    return it->second.get();
  }

  auto mo = manager->allocateGlobal(globalObject->size, gv, byTid,
                                    gv->getAlignment());
  globalObject->threadLocalMemory.emplace(byTid, mo);
  return mo;
}

ref<ConstantExpr> GlobalObjectsMap::lookupGlobal(MemoryManager *manager,
                                                 const llvm::GlobalValue *gv,
                                                 const ThreadId &byTid) {
  auto globalObject = findObject(gv);

  if (globalObject == nullptr) {
    return Expr::createPointer(0);
  }

  // All other types do not change based on the calling thread
  if (globalObject->type != ReferencedType::Data || !gv->isThreadLocal()) {
    return globalObject->address;
  }

  assert(isa<llvm::GlobalVariable>(gv));
  return lookupGlobalMemoryObject(manager, cast<llvm::GlobalVariable>(gv), byTid)->getBaseExpr();
}

GlobalObjectsMap::GlobalObjectReference *
GlobalObjectsMap::findObject(const llvm::GlobalValue *gv) {
  auto it = globalObjects.find(gv);
  if (it != globalObjects.end()) {
    return &it->second;
  }

  return nullptr;
}

void GlobalObjectsMap::clear() noexcept { globalObjects.clear(); }
