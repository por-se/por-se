#include "klee/Expr/ArrayCache.h"

namespace klee {

ArrayCache::~ArrayCache() {
  // Free Allocated Array objects
  for (auto ai = cachedSymbolicArrays.begin(), e = cachedSymbolicArrays.end();
       ai != e; ++ai) {
    delete *ai;
  }
  for (auto ai = cachedConcreteArrays.begin(), e = cachedConcreteArrays.end();
       ai != e; ++ai) {
    delete *ai;
  }
}

const Array *
ArrayCache::CreateArray(const std::string &_name, uint64_t _size,
                        const ref<ConstantExpr> *constantValuesBegin,
                        const ref<ConstantExpr> *constantValuesEnd,
                        Expr::Width _domain, Expr::Width _range) {

  const Array *array = new Array(_name, _size, constantValuesBegin,
                                 constantValuesEnd, _domain, _range);
  if (array->isSymbolicArray()) {
    auto [it, success] = cachedSymbolicArrays.insert(array);
    if (success) {
      // Cache miss
      return array;
    }
    // Cache hit
    delete array;
    array = *it;
    assert(array->isSymbolicArray() &&
           "Cached symbolic array is no longer symbolic");
    return array;
  } else {
    assert(array->isConstantArray());
    auto [it, success] = cachedConcreteArrays.insert(array);
    if (success) {
      // Cache miss
      return array;
    }
    // Cache hit
    delete array;
    array = *it;
    return array;
  }
}
}
