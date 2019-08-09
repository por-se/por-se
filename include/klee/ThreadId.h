#ifndef KLEE_THREADID_H
#define KLEE_THREADID_H

#include <por/thread_id.h>

#include "llvm/Support/raw_ostream.h"

// This file is basically only a forward definition of the por threadid
namespace klee {
  typedef por::thread_id ThreadId;
  llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const ThreadId &tid);
}

#endif // KLEE_THREADID_H
