#ifndef KLEE_GLOBALOBJECTSMAP_H
#define KLEE_GLOBALOBJECTSMAP_H

#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalObject.h"

#include "klee/Expr/Expr.h"
#include "klee/Thread.h"
#include "klee/ThreadId.h"
#include "Memory.h"

namespace klee {
  class GlobalObjectsMap {
    private:
      enum class ReferencedType {
        Alias,
        Function,
        Data
      };

      // Wrapper object that is added for every global that klee keeps track of
      class GlobalObjectReference {
        friend class GlobalObjectsMap;

        ReferencedType type;
        const llvm::GlobalValue* value;

        ref<ConstantExpr> address;
        std::size_t size;
        std::map<const ThreadId, MemoryObject*> threadLocalMemory;

        GlobalObjectReference(const llvm::Function* f, ref<ConstantExpr> addr);
        GlobalObjectReference(const llvm::GlobalAlias* a, ref<ConstantExpr> addr);
        GlobalObjectReference(const llvm::GlobalValue* v, std::size_t size);

        const llvm::Function* getFunction();
        const llvm::GlobalAlias* getAlias();
        const llvm::GlobalValue* getGlobalValue();
        MemoryObject* getMemoryObject(const ThreadId& tid);
      };

      /// The memory manager that should manage the global objects
      // -> used for allocating thread local globals
      // -> lifecycle managed by the `Executor`
      MemoryManager* memoryManager = nullptr;

      std::map<const llvm::GlobalValue*, GlobalObjectReference> globalObjects;

    public:
      explicit GlobalObjectsMap(MemoryManager* manager);

      void registerFunction(const llvm::Function* func, ref<ConstantExpr> addr);
      void registerAlias(const llvm::GlobalAlias* alias, ref<ConstantExpr> addr);
      const MemoryObject *registerGlobalData(const llvm::GlobalValue *gv, std::size_t size, std::size_t alignment);

      const MemoryObject* lookupGlobalMemoryObject(const llvm::GlobalValue* gv, const ThreadId& byTid);
      ref<ConstantExpr> lookupGlobal(const llvm::GlobalValue* gv, const ThreadId& byTid);

    private:
      GlobalObjectReference* findObject(const llvm::GlobalValue* gv);
  };
}

#endif // KLEE_GLOBALOBJECTSMAP_H
