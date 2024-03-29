//===-- SpecialFunctionHandler.cpp ----------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "SpecialFunctionHandler.h"

#include "Executor.h"
#include "Memory.h"
#include "MemoryManager.h"
#include "TimingSolver.h"

#include "klee/ExecutionState.h"
#include "klee/Thread.h"

#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/KModule.h"
#include "klee/Internal/Support/Debug.h"
#include "klee/Internal/Support/ErrorHandling.h"
#include "klee/OptionCategories.h"
#include "klee/PorCmdLine.h"
#include "klee/Solver/SolverCmdLine.h"

#include "llvm/ADT/Twine.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Module.h"

#include <errno.h>
#include <sstream>
#include <vector>

using namespace llvm;
using namespace klee;

namespace {
cl::opt<bool>
    ReadablePosix("readable-posix-inputs", cl::init(false),
                  cl::desc("Prefer creation of POSIX inputs (command-line "
                           "arguments, files, etc.) with human readable bytes. "
                           "Note: option is expensive when creating lots of "
                           "tests (default=false)"),
                  cl::cat(TestGenCat));

cl::opt<bool>
    SilentKleeAssume("silent-klee-assume", cl::init(false),
                     cl::desc("Silently terminate paths with an infeasible "
                              "condition given to klee_assume() rather than "
                              "emitting an error (default=false)"),
                     cl::cat(TerminationCat));

} // namespace

/// \todo Almost all of the demands in this file should be replaced
/// with terminateState calls.

///

// FIXME: We are more or less committed to requiring an intrinsic
// library these days. We can move some of this stuff there,
// especially things like realloc which have complicated semantics
// w.r.t. forking. Among other things this makes delayed query
// dispatch easier to implement.
static SpecialFunctionHandler::HandlerInfo handlerInfo[] = {
#define add(name, handler, ret) { name, \
                                  &SpecialFunctionHandler::handler, \
                                  false, ret, false }
#define addDNR(name, handler) { name, \
                                &SpecialFunctionHandler::handler, \
                                true, false, false }
  addDNR("__assert_rtn", handleAssertFail),
  addDNR("__assert_fail", handleAssertFail),
  addDNR("__assert", handleAssertFail),
  addDNR("_assert", handleAssert),
  addDNR("abort", handleAbort),
  addDNR("_exit", handleExit),
  addDNR("exit", handleExit),
  // { "exit", &SpecialFunctionHandler::handleExit, true, false, true },
  addDNR("klee_abort", handleAbort),
  addDNR("klee_silent_exit", handleSilentExit),
  addDNR("klee_report_error", handleReportError),
  add("calloc", handleCalloc, true),
  add("free", handleFree, false),
  add("klee_assume", handleAssume, false),
  add("klee_check_memory_access", handleCheckMemoryAccess, false),
  add("klee_disable_memory_state", handleDisableMemoryState, false),
  add("klee_enable_memory_state", handleEnableMemoryState, false),
  add("klee_get_valuef", handleGetValue, true),
  add("klee_get_valued", handleGetValue, true),
  add("klee_get_valuel", handleGetValue, true),
  add("klee_get_valuell", handleGetValue, true),
  add("klee_get_value_i32", handleGetValue, true),
  add("klee_get_value_i64", handleGetValue, true),
  add("klee_define_fixed_object", handleDefineFixedObject, false),
  add("klee_get_obj_size", handleGetObjSize, true),
#ifndef __APPLE__
  add("__errno_location", handleErrnoLocation, true),
#else
  add("__error", handleErrnoLocation, true),
#endif
  add("klee_is_symbolic", handleIsSymbolic, true),
  add("klee_make_symbolic", handleMakeSymbolic, false),
  add("klee_mark_global", handleMarkGlobal, false),
  add("klee_prefer_cex", handlePreferCex, false),
  add("klee_posix_prefer_cex", handlePosixPreferCex, false),
  add("klee_print_expr", handlePrintExpr, false),
  add("klee_print_range", handlePrintRange, false),
  add("klee_set_forking", handleSetForking, false),
  add("klee_stack_trace", handleStackTrace, false),
  add("klee_warning", handleWarning, false),
  add("klee_warning_once", handleWarningOnce, false),
  add("klee_create_thread", handleCreateThread, false),
  addDNR("klee_exit_thread", handleExitThread),
  add("klee_por_thread_join", handlePorThreadJoin, false),

  add("klee_lock_acquire", handleLockAcquire, false),
  add("klee_lock_release", handleLockRelease, false),
  add("klee_cond_wait", handleCondWait, false),
  add("klee_cond_signal", handleCondSignal, false),
  add("klee_cond_broadcast", handleCondBroadcast, false),

  add("malloc", handleMalloc, true),
  add("memalign", handleMemalign, true),
  add("realloc", handleRealloc, true),

  add("klee_output", handleOutput, true),
  add("getpid", handleGetPid, true),
  add("getppid", handleGetPPid, true),
  add("getuid", handleGetUid, true),
  add("geteuid", handleGetEUid, true),
  add("getgid", handleGetGid, true),
  add("getegid", handleGetEGid, true),

  // operator delete[](void*)
  add("_ZdaPv", handleDeleteArray, false),
  // operator delete(void*)
  add("_ZdlPv", handleDelete, false),

  // operator new[](unsigned int)
  add("_Znaj", handleNewArray, true),
  // operator new(unsigned int)
  add("_Znwj", handleNew, true),

  // FIXME-64: This is wrong for 64-bit long...

  // operator new[](unsigned long)
  add("_Znam", handleNewArray, true),
  // operator new(unsigned long)
  add("_Znwm", handleNew, true),

  // Run clang with -fsanitize=signed-integer-overflow and/or
  // -fsanitize=unsigned-integer-overflow
  add("__ubsan_handle_add_overflow", handleAddOverflow, false),
  add("__ubsan_handle_sub_overflow", handleSubOverflow, false),
  add("__ubsan_handle_mul_overflow", handleMulOverflow, false),
  add("__ubsan_handle_divrem_overflow", handleDivRemOverflow, false),

#undef addDNR
#undef add
};

SpecialFunctionHandler::const_iterator SpecialFunctionHandler::begin() {
  return SpecialFunctionHandler::const_iterator(handlerInfo);
}

SpecialFunctionHandler::const_iterator SpecialFunctionHandler::end() {
  // NULL pointer is sentinel
  return SpecialFunctionHandler::const_iterator(0);
}

SpecialFunctionHandler::const_iterator& SpecialFunctionHandler::const_iterator::operator++() {
  ++index;
  if ( index >= SpecialFunctionHandler::size())
  {
    // Out of range, return .end()
    base=0; // Sentinel
    index=0;
  }

  return *this;
}

int SpecialFunctionHandler::size() {
	return sizeof(handlerInfo)/sizeof(handlerInfo[0]);
}

SpecialFunctionHandler::SpecialFunctionHandler(Executor &_executor) 
  : executor(_executor) {}

void SpecialFunctionHandler::prepare(
    std::vector<const char *> &preservedFunctions) {
  unsigned N = size();

  for (unsigned i=0; i<N; ++i) {
    HandlerInfo &hi = handlerInfo[i];
    Function *f = executor.kmodule->module->getFunction(hi.name);

    // No need to create if the function doesn't exist, since it cannot
    // be called in that case.
    if (f && (!hi.doNotOverride || f->isDeclaration())) {
      preservedFunctions.push_back(hi.name);
      // Make sure NoReturn attribute is set, for optimization and
      // coverage counting.
      if (hi.doesNotReturn)
        f->addFnAttr(Attribute::NoReturn);

      // Change to a declaration since we handle internally (simplifies
      // module and allows deleting dead code).
      if (!f->isDeclaration())
        f->deleteBody();
    }
  }
}

void SpecialFunctionHandler::bind() {
  unsigned N = sizeof(handlerInfo)/sizeof(handlerInfo[0]);

  for (unsigned i=0; i<N; ++i) {
    HandlerInfo &hi = handlerInfo[i];
    Function *f = executor.kmodule->module->getFunction(hi.name);
    
    if (f && (!hi.doNotOverride || f->isDeclaration()))
      handlers[f] = std::make_pair(hi.handler, hi.hasReturnValue);
  }
}


bool SpecialFunctionHandler::handle(ExecutionState &state, 
                                    Function *f,
                                    KInstruction *target,
                                    std::vector< ref<Expr> > &arguments) {
  handlers_ty::iterator it = handlers.find(f);
  if (it != handlers.end()) {    
    Handler h = it->second.first;
    bool hasReturnValue = it->second.second;
     // FIXME: Check this... add test?
    if (!hasReturnValue && !target->inst->use_empty()) {
      executor.terminateStateOnExecError(state, 
                                         "expected return value from void special function");
    } else {
      (this->*h)(state, target, arguments);
    }
    return true;
  } else {
    return false;
  }
}

/****/

// reads a concrete string from memory
std::string 
SpecialFunctionHandler::readStringAtAddress(ExecutionState &state, 
                                            ref<Expr> addressExpr) {
  ObjectPair op;
  addressExpr = executor.toUnique(state, addressExpr);
  if (!isa<ConstantExpr>(addressExpr)) {
    executor.terminateStateOnError(
        state, "Symbolic string pointer passed to one of the klee_ functions",
        Executor::TerminateReason::User);
    return "";
  }
  ref<ConstantExpr> address = cast<ConstantExpr>(addressExpr);
  if (!state.addressSpace.resolveOne(address, op)) {
    executor.terminateStateOnError(
        state, "Invalid string pointer passed to one of the klee_ functions",
        Executor::TerminateReason::User);
    return "";
  }
  bool res __attribute__ ((unused));
  assert(executor.solver->mustBeTrue(state, 
                                     EqExpr::create(address, 
                                                    op.first->getBaseExpr()),
                                     res) &&
         res &&
         "XXX interior pointer unhandled");
  const MemoryObject *mo = op.first;
  const ObjectState *os = op.second;

  char *buf = new char[mo->size];

  unsigned i;
  for (i = 0; i < mo->size - 1; i++) {
    ref<Expr> cur = os->read8(i);
    cur = executor.toUnique(state, cur);
    assert(isa<ConstantExpr>(cur) && 
           "hit symbolic char while reading concrete string");
    buf[i] = cast<ConstantExpr>(cur)->getZExtValue(8);
  }
  buf[i] = 0;
  
  std::string result(buf);
  delete[] buf;
  return result;
}

/****/

void SpecialFunctionHandler::handleAbort(ExecutionState &state,
                           KInstruction *target,
                           std::vector<ref<Expr> > &arguments) {
  assert(arguments.size()==0 && "invalid number of arguments to abort");
  executor.terminateStateOnError(state, "abort failure", Executor::Abort);
}

void SpecialFunctionHandler::handleExit(ExecutionState &state,
                           KInstruction *target,
                           std::vector<ref<Expr> > &arguments) {
  assert(arguments.size()==1 && "invalid number of arguments to exit");

  executor.exitCurrentThread(state, true);
  if (!executor.porEventManager.registerThreadExit(state, state.tid(), false)) {
    executor.terminateStateSilently(state);
    return;
  }
}

void SpecialFunctionHandler::handleSilentExit(ExecutionState &state,
                                              KInstruction *target,
                                              std::vector<ref<Expr> > &arguments) {
  assert(arguments.size()==1 && "invalid number of arguments to exit");
  executor.terminateState(state);
}

void SpecialFunctionHandler::handleAssert(ExecutionState &state,
                                          KInstruction *target,
                                          std::vector<ref<Expr> > &arguments) {
  assert(arguments.size()==3 && "invalid number of arguments to _assert");  
  executor.terminateStateOnError(state,
				 "ASSERTION FAIL: " + readStringAtAddress(state, arguments[0]),
				 Executor::Assert);
}

void SpecialFunctionHandler::handleAssertFail(ExecutionState &state,
                                              KInstruction *target,
                                              std::vector<ref<Expr> > &arguments) {
  assert(arguments.size()==4 && "invalid number of arguments to __assert_fail");
  executor.terminateStateOnError(state,
				 "ASSERTION FAIL: " + readStringAtAddress(state, arguments[0]),
				 Executor::Assert);
}

void SpecialFunctionHandler::handleReportError(ExecutionState &state,
                                               KInstruction *target,
                                               std::vector<ref<Expr> > &arguments) {
  assert(arguments.size()==4 && "invalid number of arguments to klee_report_error");
  
  // arguments[0], arguments[1] are file, line
  executor.terminateStateOnError(state,
				 readStringAtAddress(state, arguments[2]),
				 Executor::ReportError,
				 readStringAtAddress(state, arguments[3]).c_str());
}

void SpecialFunctionHandler::handleNew(ExecutionState &state,
                         KInstruction *target,
                         std::vector<ref<Expr> > &arguments) {
  // XXX should type check args
  assert(arguments.size()==1 && "invalid number of arguments to new");

  executor.executeAlloc(state, arguments[0], false, target);
}

void SpecialFunctionHandler::handleDelete(ExecutionState &state,
                            KInstruction *target,
                            std::vector<ref<Expr> > &arguments) {
  // FIXME: Should check proper pairing with allocation type (malloc/free,
  // new/delete, new[]/delete[]).

  // XXX should type check args
  assert(arguments.size()==1 && "invalid number of arguments to delete");
  executor.executeFree(state, arguments[0]);
}

void SpecialFunctionHandler::handleNewArray(ExecutionState &state,
                              KInstruction *target,
                              std::vector<ref<Expr> > &arguments) {
  // XXX should type check args
  assert(arguments.size()==1 && "invalid number of arguments to new[]");
  executor.executeAlloc(state, arguments[0], false, target);
}

void SpecialFunctionHandler::handleDeleteArray(ExecutionState &state,
                                 KInstruction *target,
                                 std::vector<ref<Expr> > &arguments) {
  // XXX should type check args
  assert(arguments.size()==1 && "invalid number of arguments to delete[]");
  executor.executeFree(state, arguments[0]);
}

void SpecialFunctionHandler::handleMalloc(ExecutionState &state,
                                  KInstruction *target,
                                  std::vector<ref<Expr> > &arguments) {
  // XXX should type check args
  assert(arguments.size()==1 && "invalid number of arguments to malloc");
  executor.executeAlloc(state, arguments[0], false, target);
}

void SpecialFunctionHandler::handleMemalign(ExecutionState &state,
                                            KInstruction *target,
                                            std::vector<ref<Expr>> &arguments) {
  if (arguments.size() != 2) {
    executor.terminateStateOnError(state,
      "Incorrect number of arguments to memalign(size_t alignment, size_t size)",
      Executor::User);
    return;
  }

  std::pair<ref<Expr>, ref<Expr>> alignmentRangeExpr =
      executor.solver->getRange(state, arguments[0]);
  ref<Expr> alignmentExpr = alignmentRangeExpr.first;
  auto alignmentConstExpr = dyn_cast<ConstantExpr>(alignmentExpr);

  if (!alignmentConstExpr) {
    executor.terminateStateOnError(state,
      "Could not determine size of symbolic alignment",
      Executor::User);
    return;
  }

  uint64_t alignment = alignmentConstExpr->getZExtValue();

  // Warn, if the expression has more than one solution
  if (alignmentRangeExpr.first != alignmentRangeExpr.second) {
    klee_warning_once(
        0, "Symbolic alignment for memalign. Choosing smallest alignment");
  }

  executor.executeAlloc(state, arguments[1], false, target, false, 0,
                        alignment);
}

void SpecialFunctionHandler::handleAssume(ExecutionState &state,
                            KInstruction *target,
                            std::vector<ref<Expr> > &arguments) {
  assert(arguments.size()==1 && "invalid number of arguments to klee_assume");
  
  ref<Expr> e = arguments[0];
  
  if (e->getWidth() != Expr::Bool)
    e = NeExpr::create(e, ConstantExpr::create(0, e->getWidth()));
  
  bool res;
  bool success __attribute__ ((unused)) = executor.solver->mustBeFalse(state, e, res);
  assert(success && "FIXME: Unhandled solver failure");
  if (res) {
    if (SilentKleeAssume) {
      executor.terminateState(state);
    } else {
      executor.terminateStateOnError(state,
                                     "invalid klee_assume call (provably false)",
                                     Executor::User);
    }
  } else {
    executor.addConstraint(state, e);
  }
}

void SpecialFunctionHandler::handleIsSymbolic(ExecutionState &state,
                                KInstruction *target,
                                std::vector<ref<Expr> > &arguments) {
  assert(arguments.size()==1 && "invalid number of arguments to klee_is_symbolic");

  executor.bindLocal(target, state, 
                     ConstantExpr::create(!isa<ConstantExpr>(arguments[0]),
                                          Expr::Int32));
}

void SpecialFunctionHandler::handlePreferCex(ExecutionState &state,
                                             KInstruction *target,
                                             std::vector<ref<Expr> > &arguments) {
  assert(arguments.size()==2 &&
         "invalid number of arguments to klee_prefex_cex");

  ref<Expr> cond = arguments[1];
  if (cond->getWidth() != Expr::Bool)
    cond = NeExpr::create(cond, ConstantExpr::alloc(0, cond->getWidth()));

  Executor::ExactResolutionList rl;
  executor.resolveExact(state, arguments[0], rl, "prefex_cex");
  
  assert(rl.size() == 1 &&
         "prefer_cex target must resolve to precisely one object");

  rl[0].first.first->cexPreferences.push_back(cond);
}

void SpecialFunctionHandler::handlePosixPreferCex(ExecutionState &state,
                                             KInstruction *target,
                                             std::vector<ref<Expr> > &arguments) {
  if (ReadablePosix)
    return handlePreferCex(state, target, arguments);
}

void SpecialFunctionHandler::handlePrintExpr(ExecutionState &state,
                                  KInstruction *target,
                                  std::vector<ref<Expr> > &arguments) {
  assert(arguments.size()==2 &&
         "invalid number of arguments to klee_print_expr");

  std::string msg_str = readStringAtAddress(state, arguments[0]);
  llvm::errs() << msg_str << ":" << arguments[1] << "\n";
}

void SpecialFunctionHandler::handleSetForking(ExecutionState &state,
                                              KInstruction *target,
                                              std::vector<ref<Expr> > &arguments) {
  assert(arguments.size()==1 &&
         "invalid number of arguments to klee_set_forking");
  ref<Expr> value = executor.toUnique(state, arguments[0]);
  
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(value)) {
    state.forkDisabled = CE->isZero();
  } else {
    executor.terminateStateOnError(state, 
                                   "klee_set_forking requires a constant arg",
                                   Executor::User);
  }
}

void SpecialFunctionHandler::handleStackTrace(ExecutionState &state,
                                              KInstruction *target,
                                              std::vector<ref<Expr> > &arguments) {
  state.dumpStack(outs());
}

void SpecialFunctionHandler::handleWarning(ExecutionState &state,
                                           KInstruction *target,
                                           std::vector<ref<Expr> > &arguments) {
  assert(arguments.size()==1 && "invalid number of arguments to klee_warning");

  std::string msg_str = readStringAtAddress(state, arguments[0]);
  klee_warning("%s: %s", state.stackFrame().kf->function->getName().data(),
               msg_str.c_str());
}

void SpecialFunctionHandler::handleWarningOnce(ExecutionState &state,
                                               KInstruction *target,
                                               std::vector<ref<Expr> > &arguments) {
  assert(arguments.size()==1 &&
         "invalid number of arguments to klee_warning_once");

  std::string msg_str = readStringAtAddress(state, arguments[0]);
  klee_warning_once(0, "%s: %s", state.stackFrame().kf->function->getName().data(),
                    msg_str.c_str());
}

void SpecialFunctionHandler::handlePrintRange(ExecutionState &state,
                                  KInstruction *target,
                                  std::vector<ref<Expr> > &arguments) {
  assert(arguments.size()==2 &&
         "invalid number of arguments to klee_print_range");

  std::string msg_str = readStringAtAddress(state, arguments[0]);
  llvm::errs() << msg_str << ":" << arguments[1];
  if (!isa<ConstantExpr>(arguments[1])) {
    // FIXME: Pull into a unique value method?
    ref<ConstantExpr> value;
    bool success __attribute__ ((unused)) = executor.solver->getValue(state, arguments[1], value);
    assert(success && "FIXME: Unhandled solver failure");
    bool res;
    success = executor.solver->mustBeTrue(state, 
                                          EqExpr::create(arguments[1], value), 
                                          res);
    assert(success && "FIXME: Unhandled solver failure");
    if (res) {
      llvm::errs() << " == " << value;
    } else { 
      llvm::errs() << " ~= " << value;
      std::pair< ref<Expr>, ref<Expr> > res =
        executor.solver->getRange(state, arguments[1]);
      llvm::errs() << " (in [" << res.first << ", " << res.second <<"])";
    }
  }
  llvm::errs() << "\n";
}

void SpecialFunctionHandler::handleGetObjSize(ExecutionState &state,
                                  KInstruction *target,
                                  std::vector<ref<Expr> > &arguments) {
  // XXX should type check args
  assert(arguments.size()==1 &&
         "invalid number of arguments to klee_get_obj_size");
  Executor::ExactResolutionList rl;
  executor.resolveExact(state, arguments[0], rl, "klee_get_obj_size");
  for (Executor::ExactResolutionList::iterator it = rl.begin(), 
         ie = rl.end(); it != ie; ++it) {
    executor.bindLocal(
        target, *it->second,
        ConstantExpr::create(it->first.first->size,
                             executor.kmodule->targetData->getTypeSizeInBits(
                                 target->inst->getType())));
  }
}


void SpecialFunctionHandler::handleErrnoLocation(
    ExecutionState &state, KInstruction *target,
    std::vector<ref<Expr> > &arguments) {
  // Returns the address of the errno variable
  assert(arguments.size() == 0 &&
         "invalid number of arguments to __errno_location/__error");

  const MemoryObject* thErrno = state.errnoMo();

  executor.bindLocal(target, state, thErrno->getBaseExpr());
}
void SpecialFunctionHandler::handleCalloc(ExecutionState &state,
                            KInstruction *target,
                            std::vector<ref<Expr> > &arguments) {
  // XXX should type check args
  assert(arguments.size()==2 &&
         "invalid number of arguments to calloc");

  ref<Expr> size = MulExpr::create(arguments[0],
                                   arguments[1]);
  executor.executeAlloc(state, size, false, target, true);
}

void SpecialFunctionHandler::handleRealloc(ExecutionState &state,
                            KInstruction *target,
                            std::vector<ref<Expr> > &arguments) {
  // XXX should type check args
  assert(arguments.size()==2 &&
         "invalid number of arguments to realloc");
  ref<Expr> address = arguments[0];
  ref<Expr> size = arguments[1];

  Executor::StatePair zeroSize = executor.fork(state, 
                                               Expr::createIsZero(size), 
                                               true);
  
  if (zeroSize.first) { // size == 0
    if (zeroSize.first != &state) {
      // local event after fork() is only added after executeInstruction() has finished
      // for the purpose of data race detection, temporarily set porNode of new state
      assert(zeroSize.first->porNode == nullptr);
      zeroSize.first->porNode = state.porNode;
    }
    executor.executeFree(*zeroSize.first, address, target);
    if (zeroSize.first != &state) {
      // reset porNode to be updated after executeInstruction()
      zeroSize.first->porNode = nullptr;
    }
  }
  if (zeroSize.second) { // size != 0
    Executor::StatePair zeroPointer = executor.fork(*zeroSize.second, 
                                                    Expr::createIsZero(address), 
                                                    true);
    
    if (zeroPointer.first) { // address == 0
      if (zeroPointer.first != &state) {
        // local event after fork() is only added after executeInstruction() has finished
        // for the purpose of data race detection, temporarily set porNode of new state
        assert(zeroPointer.first->porNode == nullptr);
        zeroPointer.first->porNode = state.porNode;
      }
      executor.executeAlloc(*zeroPointer.first, size, false, target);
      if (zeroPointer.first != &state) {
        // reset porNode to be updated after executeInstruction()
        zeroPointer.first->porNode = nullptr;
      }
    } 
    if (zeroPointer.second) { // address != 0
      Executor::ExactResolutionList rl;
      executor.resolveExact(*zeroPointer.second, address, rl, "realloc");
      
      for (Executor::ExactResolutionList::iterator it = rl.begin(), 
             ie = rl.end(); it != ie; ++it) {
        if (it->second != &state) {
          // local event after fork() is only added after executeInstruction() has finished
          // for the purpose of data race detection, temporarily set porNode of new state
          assert(it->second->porNode == nullptr);
          it->second->porNode = state.porNode;
        }
        executor.executeAlloc(*it->second, size, false, target, false, 
                              it->first.second);
        if (it->second != &state) {
          // reset porNode to be updated after executeInstruction()
          it->second->porNode = nullptr;
        }
      }
    }
  }
}

void SpecialFunctionHandler::handleFree(ExecutionState &state,
                          KInstruction *target,
                          std::vector<ref<Expr> > &arguments) {
  // XXX should type check args
  assert(arguments.size()==1 &&
         "invalid number of arguments to free");
  executor.executeFree(state, arguments[0]);
}

void SpecialFunctionHandler::handleCheckMemoryAccess(ExecutionState &state,
                                                     KInstruction *target,
                                                     std::vector<ref<Expr> > 
                                                       &arguments) {
  assert(arguments.size()==2 &&
         "invalid number of arguments to klee_check_memory_access");

  ref<Expr> address = executor.toUnique(state, arguments[0]);
  ref<Expr> size = executor.toUnique(state, arguments[1]);
  if (!isa<ConstantExpr>(address) || !isa<ConstantExpr>(size)) {
    executor.terminateStateOnError(state, 
                                   "check_memory_access requires constant args",
				   Executor::User);
  } else {
    ObjectPair op;

    if (!state.addressSpace.resolveOne(cast<ConstantExpr>(address), op)) {
      executor.terminateStateOnError(state,
                                     "check_memory_access: memory error",
				     Executor::Ptr, NULL,
                                     executor.getAddressInfo(state, address));
    } else {
      ref<Expr> chk = 
        op.first->getBoundsCheckPointer(address, 
                                        cast<ConstantExpr>(size)->getZExtValue());
      if (!chk->isTrue()) {
        executor.terminateStateOnError(state,
                                       "check_memory_access: memory error",
				       Executor::Ptr, NULL,
                                       executor.getAddressInfo(state, address));
      }
    }
  }
}

void SpecialFunctionHandler::handleDisableMemoryState(ExecutionState &state,
                                                          KInstruction *target,
                                                          std::vector<ref<Expr>>
                                                            &arguments) {
  if (EnableCutoffEvents) {
    state.memoryState.disable();
    klee_warning_once(target, "disabling memory state");
  }
}

void SpecialFunctionHandler::handleEnableMemoryState(ExecutionState &state,
                                                          KInstruction *target,
                                                          std::vector<ref<Expr>>
                                                            &arguments) {
  if (EnableCutoffEvents) {
    state.memoryState.enable();
    klee_warning_once(target, "enabling memory state");
  }
}

void SpecialFunctionHandler::handleGetValue(ExecutionState &state,
                                            KInstruction *target,
                                            std::vector<ref<Expr> > &arguments) {
  assert(arguments.size()==1 &&
         "invalid number of arguments to klee_get_value");

  executor.executeGetValue(state, arguments[0], target);
}

void SpecialFunctionHandler::handleDefineFixedObject(ExecutionState &state,
                                                     KInstruction *target,
                                                     std::vector<ref<Expr> > &arguments) {
  assert(arguments.size()==2 &&
         "invalid number of arguments to klee_define_fixed_object");
  assert(isa<ConstantExpr>(arguments[0]) &&
         "expect constant address argument to klee_define_fixed_object");
  assert(isa<ConstantExpr>(arguments[1]) &&
         "expect constant size argument to klee_define_fixed_object");

  uint64_t address = cast<ConstantExpr>(arguments[0])->getZExtValue();
  uint64_t size = cast<ConstantExpr>(arguments[1])->getZExtValue();
  MemoryObject *mo = executor.memory->allocateFixed(address, size,
                                                    state.prevPc()->inst,
                                                    state.thread(),
                                                    state.stackFrameIndex());
  ObjectState *os = executor.bindObjectInState(state, mo, false);
  mo->isUserSpecified = true; // XXX hack;
  if (EnableCutoffEvents)
    state.memoryState.registerWrite(*mo, *os);
}

void SpecialFunctionHandler::handleMakeSymbolic(ExecutionState &state,
                                                KInstruction *target,
                                                std::vector<ref<Expr> > &arguments) {
  std::string name;

  if (arguments.size() != 3) {
    executor.terminateStateOnError(state, "Incorrect number of arguments to klee_make_symbolic(void*, size_t, char*)", Executor::User);
    return;
  }

  name = arguments[2]->isZero() ? "" : readStringAtAddress(state, arguments[2]);

  if (name.length() == 0) {
    name = "unnamed";
    klee_warning("klee_make_symbolic: renamed empty name to \"unnamed\"");
  }

  Executor::ExactResolutionList rl;
  executor.resolveExact(state, arguments[0], rl, "make_symbolic");
  
  for (Executor::ExactResolutionList::iterator it = rl.begin(), 
         ie = rl.end(); it != ie; ++it) {
    const MemoryObject *mo = it->first.first;
    mo->setName(name);
    
    const ObjectState *old = it->first.second;
    ExecutionState *s = it->second;
    
    if (old->readOnly) {
      executor.terminateStateOnError(*s, "cannot make readonly object symbolic",
                                     Executor::User);
      return;
    } 

    // FIXME: Type coercion should be done consistently somewhere.
    bool res;
    bool success __attribute__ ((unused)) =
      executor.solver->mustBeTrue(*s, 
                                  EqExpr::create(ZExtExpr::create(arguments[1],
                                                                  Context::get().getPointerWidth()),
                                                 mo->getSizeExpr()),
                                  res);
    assert(success && "FIXME: Unhandled solver failure");
    
    if (res) {
      executor.executeMakeSymbolic(*s, arguments[0], mo, old, name);
    } else {      
      executor.terminateStateOnError(*s, 
                                     "wrong size given to klee_make_symbolic[_name]", 
                                     Executor::User);
    }
  }
}

void SpecialFunctionHandler::handleMarkGlobal(ExecutionState &state,
                                              KInstruction *target,
                                              std::vector<ref<Expr> > &arguments) {
  assert(arguments.size()==1 &&
         "invalid number of arguments to klee_mark_global");  

  Executor::ExactResolutionList rl;
  executor.resolveExact(state, arguments[0], rl, "mark_global");
  
  for (Executor::ExactResolutionList::iterator it = rl.begin(), 
         ie = rl.end(); it != ie; ++it) {
    const MemoryObject *mo = it->first.first;
    assert(!mo->isLocal);
    mo->isGlobal = true;
  }
}

void SpecialFunctionHandler::handleAddOverflow(ExecutionState &state,
                                               KInstruction *target,
                                               std::vector<ref<Expr> > &arguments) {
  executor.terminateStateOnError(state, "overflow on addition",
                                 Executor::Overflow);
}

void SpecialFunctionHandler::handleSubOverflow(ExecutionState &state,
                                               KInstruction *target,
                                               std::vector<ref<Expr> > &arguments) {
  executor.terminateStateOnError(state, "overflow on subtraction",
                                 Executor::Overflow);
}

void SpecialFunctionHandler::handleMulOverflow(ExecutionState &state,
                                               KInstruction *target,
                                               std::vector<ref<Expr> > &arguments) {
  executor.terminateStateOnError(state, "overflow on multiplication",
                                 Executor::Overflow);
}

void SpecialFunctionHandler::handleDivRemOverflow(ExecutionState &state,
                                               KInstruction *target,
                                               std::vector<ref<Expr> > &arguments) {
  executor.terminateStateOnError(state, "overflow on division or remainder",
                                 Executor::Overflow);
}

/* Threading support */

void SpecialFunctionHandler::handleCreateThread(ExecutionState &state,
                                                KInstruction *target,
                                                std::vector<ref<Expr> > &arguments) {
  assert(arguments.size() == 2 && "invalid number of arguments to klee_create_thread");

  // We only handle const function pointers
  if (!isa<ConstantExpr>(arguments[0])) {
    executor.terminateStateOnError(state, "klee_create_thread", Executor::User);
    return;
  }

  auto funcAddress = cast<ConstantExpr>(arguments[0])->getZExtValue();

  // The addresses of the function in the program are equal to the addresses
  // of the llvm functions
  auto funcPointer = reinterpret_cast<llvm::Function*>(funcAddress);
  auto kfuncPair = executor.kmodule->functionMap.find(funcPointer);
  if (kfuncPair == executor.kmodule->functionMap.end()) {
    executor.terminateStateOnError(state, "klee_create_thread", Executor::User);
    return;
  }

  if (!executor.createThread(state, kfuncPair->second, arguments[1])) {
    return;
  }
}

void SpecialFunctionHandler::handleExitThread(klee::ExecutionState &state,
                                              klee::KInstruction *target,
                                              std::vector<klee::ref<klee::Expr>> &arguments) {
  assert(arguments.size() == 1 && "invalid number of arguments to klee_exit_thread - expected 1");

  ref<Expr> lidExpr = executor.toUnique(state, arguments[0]);

  if (!isa<ConstantExpr>(lidExpr)) {
    executor.terminateStateOnError(state, "klee_exit_thread", Executor::User);
    return;
  }

  auto lid = cast<ConstantExpr>(lidExpr)->getZExtValue();
  const auto& ownTid = state.tid();

  state.memoryState.unregisterAcquiredLock(lid, ownTid);
  if (!executor.porEventManager.registerLockRelease(state, lid, false, false)) {
    executor.terminateStateSilently(state);
    return;
  }

  if (state.threadState() != ThreadState::Cutoff && state.threadState() != ThreadState::Exceeded) {
    executor.exitCurrentThread(state, false);
    if (!executor.porEventManager.registerThreadExit(state, ownTid, true)) {
      executor.terminateStateSilently(state);
      return;
    }
  }
}

void SpecialFunctionHandler::handlePorThreadJoin(ExecutionState &state,
                                                 KInstruction *target,
                                                 std::vector<klee::ref<klee::Expr>> &arguments) {
  assert(arguments.size() == 1 && "invalid number of arguments to klee_por_thread_join");

  ref<Expr> expr = executor.toUnique(state, arguments[0]);
  if (!isa<ConstantExpr>(expr)) {
    executor.terminateStateOnError(state, "klee_por_thread_join", Executor::User);
    return;
  }

  if (auto result = state.getThreadByRuntimeStructPtr(expr)) {
    const Thread &thread = result->get();
    if (!executor.porEventManager.registerThreadJoin(state, thread.getThreadId())) {
      executor.terminateStateSilently(state);
      return;
    }
  } else {
    executor.terminateStateOnError(state, "klee_por_thread_join", Executor::User);
    return;
  }
}

void SpecialFunctionHandler::handleLockAcquire(ExecutionState &state,
                                               KInstruction *target,
                                               std::vector<ref<Expr>> &arguments) {
  assert(arguments.size() == 1 && "invalid number of arguments to klee_lock_acquire - expected 1");

  ref<Expr> lidExpr = executor.toUnique(state, arguments[0]);

  if (!isa<ConstantExpr>(lidExpr)) {
    executor.terminateStateOnError(state, "klee_lock_acquire", Executor::User);
    return;
  }

  auto lid = cast<ConstantExpr>(lidExpr)->getZExtValue();

  state.blockThread(Thread::wait_lock_t{lid});
}

void SpecialFunctionHandler::handleLockRelease(ExecutionState &state,
                                               KInstruction *target,
                                               std::vector<ref<Expr>> &arguments) {
  assert(arguments.size() == 1 && "invalid number of arguments to klee_lock_release - expected 1");

  ref<Expr> lidExpr = executor.toUnique(state, arguments[0]);

  if (!isa<ConstantExpr>(lidExpr)) {
    executor.terminateStateOnError(state, "klee_lock_release", Executor::User);
    return;
  }

  auto lid = cast<ConstantExpr>(lidExpr)->getZExtValue();
  const auto& ownTid = state.tid();

  // Test if the lock is acquired and whether we are the one that has the lock
  const auto& lockHeads = state.porNode->configuration().lock_heads();

  auto it = lockHeads.find(lid);
  if (it == lockHeads.end()) {
    executor.terminateStateOnError(
      state,
      "Unlock of a non-existing lock is undefined behavior",
      Executor::User
    );
    return;
  }

  const auto* e = it->second;

  if (e->kind() != por::event::event_kind::lock_acquire && e->kind() != por::event::event_kind::wait2) {
    // The last action on this lock was not an acquire -> we cannot release anything and this
    // is undefined behavior
    executor.terminateStateOnError(
      state,
      "Unlock of an unacquired lock is undefined behavior",
      Executor::User
    );
    return;
  }

  if (e->tid() != ownTid) {
    executor.terminateStateOnError(
      state,
      "Unlock of a lock that is acquired by another thread is undefined behavior",
      Executor::User
    );
    return;
  }

  state.memoryState.unregisterAcquiredLock(lid, ownTid);
  if (!executor.porEventManager.registerLockRelease(state, lid, true, false)) {
    executor.terminateStateSilently(state);
    return;
  }
}

void SpecialFunctionHandler::handleCondWait(ExecutionState &state,
                                             KInstruction *target,
                                             std::vector<ref<Expr>> &arguments) {
  assert(arguments.size() == 2 && "invalid number of arguments to klee_cond_wait - expected 2");

  ref<Expr> cidExpr = executor.toUnique(state, arguments[0]);
  ref<Expr> lidExpr = executor.toUnique(state, arguments[1]);

  if (!isa<ConstantExpr>(cidExpr) || !isa<ConstantExpr>(lidExpr)) {
    executor.terminateStateOnError(state, "klee_cond_wait", Executor::User);
    return;
  }

  auto cid = cast<ConstantExpr>(cidExpr)->getZExtValue();
  auto lid = cast<ConstantExpr>(lidExpr)->getZExtValue();

  const auto& ownTid = state.tid();

  // Test if the lock is acquired and whether we are the one that has the lock
  const auto& lockHeads = state.porNode->configuration().lock_heads();
  auto it = lockHeads.find(lid);

  // block until be receive a signal / broadcast

  // but first we have to release the mutex
  if (it == lockHeads.end()) {
    // Maybe these should be an exit error
    executor.terminateStateOnError(
      state,
      "Unlock of a non-existing lock is undefined behavior",
      Executor::User
    );
    return;
  }

  const auto* e = it->second;

  if (e->kind() != por::event::event_kind::lock_acquire && e->kind() != por::event::event_kind::wait2) {
    // The last action on this lock was not an acquire -> we cannot release anything and this
    // is undefined behavior
    executor.terminateStateOnError(
      state,
      "Unlock of an unacquired lock is undefined behavior",
      Executor::User
    );
    return;
  }

  if (e->tid() != ownTid) {
    executor.terminateStateOnError(
      state,
      "Unlock of a lock that is acquired by another thread is undefined behavior",
      Executor::User
    );
    return;
  }

  state.blockThread(Thread::wait_cv_1_t{cid, lid});
  state.memoryState.unregisterAcquiredLock(lid, ownTid);
  if (!executor.porEventManager.registerCondVarWait1(state, cid, lid)) {
    executor.terminateStateSilently(state);
    return;
  }
}

void SpecialFunctionHandler::handleCondSignal(ExecutionState &state,
                                              KInstruction *target,
                                              std::vector<ref<Expr>> &arguments) {
  assert(arguments.size() == 1 && "invalid number of arguments to klee_cond_signal - expected 1");

  ref<Expr> cidExpr = executor.toUnique(state, arguments[0]);

  if (!isa<ConstantExpr>(cidExpr)) {
    executor.terminateStateOnError(state, "klee_cond_signal", Executor::User);
    return;
  }

  auto cid = cast<ConstantExpr>(cidExpr)->getZExtValue();

  std::optional<ThreadId> choice;
  if (state.needsCatchUp()) {
    const por::event::event* event = state.peekCatchUp();
    assert(event->kind() == por::event::event_kind::signal);
    auto signal = static_cast<const por::event::signal*>(event);
    if (!signal->is_lost()) {
      assert(state.getThreadById(signal->notified_thread()));
      auto &thread = state.getThreadById(signal->notified_thread()).value().get();
      if (state.threadState(thread) != ThreadState::Cutoff && state.threadState(thread) != ThreadState::Exceeded) {
        state.blockThread(thread, Thread::wait_cv_2_t{signal->cid(), signal->wait_predecessor()->lid()});
      }
      choice = signal->notified_thread();
    }
  } else {
    for (auto &[tid, thread] : state.threads) {
      auto w = thread.isWaitingOn<Thread::wait_cv_1_t>();
      if (w && w->cond == cid) {
        if (state.threadState(thread) != ThreadState::Cutoff && state.threadState(thread) != ThreadState::Exceeded) {
          state.blockThread(thread, Thread::wait_cv_2_t{w->cond, w->lock});
        }
        choice = tid;
        break; // always take first possible choice
      }
    }
  }

  if (choice) {
    if (!executor.porEventManager.registerCondVarSignal(state, cid, choice.value())) {
      executor.terminateStateSilently(state);
      return;
    }
  } else {
    if (!executor.porEventManager.registerCondVarSignal(state, cid, {})) {
      executor.terminateStateSilently(state);
      return;
    }
  }
}

void SpecialFunctionHandler::handleCondBroadcast(ExecutionState &state,
                                                 KInstruction *target,
                                                 std::vector<ref<Expr>> &arguments) {
  assert(arguments.size() == 1 && "invalid number of arguments to klee_cond_broadcast - expected 1");

  ref<Expr> cidExpr = executor.toUnique(state, arguments[0]);

  if (!isa<ConstantExpr>(cidExpr)) {
    executor.terminateStateOnError(state, "klee_cond_broadcast", Executor::User);
    return;
  }

  auto cid = cast<ConstantExpr>(cidExpr)->getZExtValue();

  std::vector<ThreadId> notifiedThreads;

  if (state.needsCatchUp()) {
    const por::event::event* event = state.peekCatchUp();
    assert(event->kind() == por::event::event_kind::broadcast);
    auto broadcast = static_cast<const por::event::broadcast*>(event);
    for (const auto &wait1 : broadcast->wait_predecessors()) {
      assert(state.getThreadById(wait1->tid()));
      auto &thread = state.getThreadById(wait1->tid()).value().get();
      if (state.threadState(thread) != ThreadState::Cutoff && state.threadState(thread) != ThreadState::Exceeded) {
        state.blockThread(thread, Thread::wait_cv_2_t{wait1->cid(), wait1->lid()});
      }
      notifiedThreads.push_back(wait1->tid());
    }
  } else {
    for (auto &[tid, thread] : state.threads) {
      auto w = thread.isWaitingOn<Thread::wait_cv_1_t>();
      if (w && w->cond == cid) {
        if (state.threadState(thread) != ThreadState::Cutoff && state.threadState(thread) != ThreadState::Exceeded) {
          state.blockThread(thread, Thread::wait_cv_2_t{w->cond, w->lock});
        }
        notifiedThreads.push_back(tid);
      }
    }
  }

  if (!executor.porEventManager.registerCondVarBroadcast(state, cid, notifiedThreads)) {
    executor.terminateStateSilently(state);
    return;
  }
}

void SpecialFunctionHandler::handleOutput(klee::ExecutionState &state,
                                          klee::KInstruction *target,
                                          std::vector<klee::ref<klee::Expr>> &arguments) {
  assert(arguments.size() == 2 && "invalid number of arguments to klee_output - expected 2");

  auto outputTargetExpr = executor.toUnique(state, arguments[0]);
  auto outputBufferExpr = executor.toUnique(state, arguments[1]);

  if (!isa<ConstantExpr>(outputTargetExpr) || !isa<ConstantExpr>(outputBufferExpr)) {
    executor.terminateStateOnError(state, "Symbolic argument passed to klee_output", Executor::User);
    return;
  }

  auto outputTarget = cast<ConstantExpr>(outputTargetExpr)->getZExtValue();
  if (outputTarget != 1 && outputTarget != 2) {
    executor.terminateStateOnError(state, "Invalid target passed to klee_output", Executor::User);
    return;
  }

  ObjectPair op;
  ref<ConstantExpr> address = cast<ConstantExpr>(outputBufferExpr);
  if (!state.addressSpace.resolveOne(address, op)) {
    executor.terminateStateOnError(
      state,
      "Invalid buffer pointer passed to klee_output",
      Executor::TerminateReason::User
    );
    return;
  }

  const MemoryObject *mo = op.first;
  const ObjectState *os = op.second;

  if (mo->address != address->getZExtValue()) {
    executor.terminateStateOnError(
      state,
      "Interior pointer passed to klee_output",
      Executor::TerminateReason::User
    );
    return;
  }

  char *buf = new char[mo->size];

  for (size_t i = 0; i < mo->size; i++) {
    ref<Expr> cur = os->read8(i);
    cur = executor.toUnique(state, cur);

    if (!isa<ConstantExpr>(cur)) {
      delete[] buf;

      executor.terminateStateOnError(
        state,
        "Symbolic char in output buffer during klee_output",
        Executor::TerminateReason::User
      );
      return;
    }

    buf[i] = cast<ConstantExpr>(cur)->getZExtValue(8);
  }

  if (outputTarget == 1) {
    llvm::outs().write(buf, mo->size);
    llvm::outs().flush();
  } else {
    llvm::errs().write(buf, mo->size);
    llvm::errs().flush();
  }

  delete[] buf;

  executor.bindLocal(target, state, ConstantExpr::create(0, Expr::Int32));
}

#define TRIVIAL_HANDLER(handlername, funcname) \
  void SpecialFunctionHandler::handlername(klee::ExecutionState &state, \
                                            klee::KInstruction *target, \
                                            std::vector<klee::ref<klee::Expr>> &arguments) { \
    assert(arguments.empty() && "invalid number of arguments to " #funcname " - expected none"); \
    auto result = funcname(); \
    executor.bindLocal(target, state, ConstantExpr::create(result, sizeof(result) * 8)); \
  }

TRIVIAL_HANDLER(handleGetPid, getpid)
TRIVIAL_HANDLER(handleGetPPid, getppid)
TRIVIAL_HANDLER(handleGetUid, getuid)
TRIVIAL_HANDLER(handleGetEUid, geteuid)
TRIVIAL_HANDLER(handleGetGid, getgid)
TRIVIAL_HANDLER(handleGetEGid, getegid)
