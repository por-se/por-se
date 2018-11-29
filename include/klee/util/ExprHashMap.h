//===-- ExprHashMap.h -------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_EXPRHASHMAP_H
#define KLEE_EXPRHASHMAP_H

#include "klee/Expr.h"

#include <unordered_map>
#include <unordered_set>

namespace klee {

namespace util {
struct ExprHash {
  unsigned operator()(const ref<Expr> e) const { return e->hash(); }
};

struct ExprCmp {
  bool operator()(const ref<Expr> &a, const ref<Expr> &b) const {
    return a == b;
  }
};
} // namespace util

template <typename T>
using ExprHashMap =
    std::unordered_map<ref<Expr>, T, util::ExprHash, util::ExprCmp>;

using ExprHashSet =
    std::unordered_set<ref<Expr>, util::ExprHash, util::ExprCmp>;

} // namespace klee

#endif
