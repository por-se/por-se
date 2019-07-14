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

#include "klee/por/events.h"

#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/KModule.h"
#include "klee/Internal/Support/Debug.h"
#include "klee/Internal/Support/ErrorHandling.h"
#include "klee/OptionCategories.h"
#include "klee/SolverCmdLine.h"
#include "klee/StatePruningCmdLine.h"

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
  { "exit", &SpecialFunctionHandler::handleExit, true, false, true },
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
  add("klee_get_errno", handleGetErrno, true),
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
  add("klee_print_fingerprint", handlePrintFingerprint, false),
  add("klee_print_range", handlePrintRange, false),
  add("klee_set_forking", handleSetForking, false),
  add("klee_stack_trace", handleStackTrace, false),
  add("klee_warning", handleWarning, false),
  add("klee_warning_once", handleWarningOnce, false),
  add("klee_create_thread", handleCreateThread, false),
  add("klee_preempt_thread", handlePreemptThread, false),
  add("klee_toggle_thread_scheduling", handleToggleThreadScheduling, false),
  add("klee_get_thread_runtime_struct_ptr", handleGetThreadRuntimeStructPtr, true),
  addDNR("klee_exit_thread", handleExitThread),
  add("klee_wait_on", handleWaitOn, false),
  add("klee_release_waiting", handleWakeUpWaiting, false),
  add("klee_por_register_event", handlePorRegisterEvent, false),
  add("klee_por_thread_join", handlePorThreadJoin, false),
  add("klee_por_thread_exit", handlePorThreadExit, false),
  add("malloc", handleMalloc, true),
  add("memalign", handleMemalign, true),
  add("realloc", handleRealloc, true),

  add("puts", handlePuts, true),

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
  executor.exitThread(state);
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

void SpecialFunctionHandler::handlePrintFingerprint(ExecutionState &state,
                                                    KInstruction *target,
                                                    std::vector<ref<Expr>> &arguments) {
  assert(arguments.size() == 0 && "invalid number of arguments to klee_print_fingerprint");

  state.printFingerprint();
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
  Thread &thread = state.currentThread();

  std::string msg_str = readStringAtAddress(state, arguments[0]);
  klee_warning("%s: %s", thread.stack.back().kf->function->getName().data(),
               msg_str.c_str());
}

void SpecialFunctionHandler::handleWarningOnce(ExecutionState &state,
                                               KInstruction *target,
                                               std::vector<ref<Expr> > &arguments) {
  assert(arguments.size()==1 &&
         "invalid number of arguments to klee_warning_once");
  Thread &thread = state.currentThread();

  std::string msg_str = readStringAtAddress(state, arguments[0]);
  klee_warning_once(0, "%s: %s", thread.stack.back().kf->function->getName().data(),
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

void SpecialFunctionHandler::handleGetErrno(ExecutionState &state,
                                            KInstruction *target,
                                            std::vector<ref<Expr> > &arguments) {
  // XXX should type check args
  assert(arguments.size()==0 &&
         "invalid number of arguments to klee_get_errno");

  // Retrieve the memory object of the errno variable
  const MemoryObject* thErrno = state.currentThread().errnoMo;
  const ObjectState* errValue = state.addressSpace.findObject(thErrno);

  assert(errValue != nullptr && "errno should be created for every thread");

  executor.bindLocal(target, state, errValue->read(0, thErrno->size * 8));
}

void SpecialFunctionHandler::handleErrnoLocation(
    ExecutionState &state, KInstruction *target,
    std::vector<ref<Expr> > &arguments) {
  // Returns the address of the errno variable
  assert(arguments.size() == 0 &&
         "invalid number of arguments to __errno_location/__error");

  const MemoryObject* thErrno = state.currentThread().errnoMo;

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
    executor.executeFree(*zeroSize.first, address, target);   
  }
  if (zeroSize.second) { // size != 0
    Executor::StatePair zeroPointer = executor.fork(*zeroSize.second, 
                                                    Expr::createIsZero(address), 
                                                    true);
    
    if (zeroPointer.first) { // address == 0
      executor.executeAlloc(*zeroPointer.first, size, false, target);
    } 
    if (zeroPointer.second) { // address != 0
      Executor::ExactResolutionList rl;
      executor.resolveExact(*zeroPointer.second, address, rl, "realloc");
      
      for (Executor::ExactResolutionList::iterator it = rl.begin(), 
             ie = rl.end(); it != ie; ++it) {
        executor.executeAlloc(*it->second, size, false, target, false, 
                              it->first.second);
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
  if (PruneStates) {
    state.memoryState.disable();
    klee_warning_once(target, "disabling memory state of infinite loop detection");
  }
}

void SpecialFunctionHandler::handleEnableMemoryState(ExecutionState &state,
                                                          KInstruction *target,
                                                          std::vector<ref<Expr>>
                                                            &arguments) {
  if (PruneStates) {
    state.memoryState.enable();
    klee_warning_once(target, "enabling memory state of infinite loop detection");
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

  Thread &thread = state.currentThread();
  
  uint64_t address = cast<ConstantExpr>(arguments[0])->getZExtValue();
  uint64_t size = cast<ConstantExpr>(arguments[1])->getZExtValue();
  MemoryObject *mo = executor.memory->allocateFixed(address, size,
                                                    thread.prevPc->inst,
                                                    thread.getThreadId(),
                                                    thread.stack.size() - 1);
  ObjectState *os = executor.bindObjectInState(state, mo, false);
  mo->isUserSpecified = true; // XXX hack;
  if (PruneStates)
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

  const ThreadId& tid = executor.createThread(state, kfuncPair->second, arguments[1]);
  executor.porEventManager.registerThreadCreate(state, tid);
}

void SpecialFunctionHandler::handlePreemptThread(klee::ExecutionState &state,
                                                klee::KInstruction *target,
                                                std::vector<klee::ref<klee::Expr>> &arguments) {
  assert(arguments.empty() && "invalid number of arguments to klee_preempt_thread");
  executor.preemptThread(state);
}

void SpecialFunctionHandler::handleExitThread(klee::ExecutionState &state,
                                              klee::KInstruction *target,
                                              std::vector<klee::ref<klee::Expr>> &arguments) {
  assert(arguments.empty() && "invalid number of arguments to klee_exit_thread");
  executor.exitThread(state);
}

void SpecialFunctionHandler::handleToggleThreadScheduling(klee::ExecutionState &state,
                                                          klee::KInstruction *target,
                                                          std::vector<klee::ref<klee::Expr>> &arguments) {
  assert(arguments.size() == 1 && "invalid number of arguments to klee_toggle_thread_scheduling");
  ref<Expr> tid = executor.toUnique(state, arguments[0]);

  if (!isa<ConstantExpr>(tid)) {
    executor.terminateStateOnError(state, "klee_toggle_thread_scheduling", Executor::User);
    return;
  }

  executor.toggleThreadScheduling(state, cast<ConstantExpr>(tid)->getZExtValue() != 0);
}

void SpecialFunctionHandler::handleGetThreadRuntimeStructPtr(klee::ExecutionState &state,
                                                             klee::KInstruction *target,
                                                             std::vector<klee::ref<klee::Expr>> &arguments) {
  assert(arguments.empty() && "invalid number of arguments to klee_get_thread_runtime_struct_ptr");

  ref<Expr> arg = state.currentThread().getRuntimeStructPtr();
  executor.bindLocal(target, state, arg);
}

void SpecialFunctionHandler::handleWaitOn(ExecutionState &state,
                                          KInstruction *target,
                                          std::vector<klee::ref<klee::Expr>> &arguments) {
  assert(arguments.size() == 2 && "invalid number of arguments to klee_wait_on");
  ref<Expr> handleExpr = executor.toUnique(state, arguments[0]);
  ref<Expr> wait1LockExpr = executor.toUnique(state, arguments[1]);

  if (!isa<ConstantExpr>(handleExpr)) {
    executor.terminateStateOnError(state, "klee_wait_on", Executor::User);
    return;
  }

  if (!isa<ConstantExpr>(wait1LockExpr)) {
    executor.terminateStateOnError(state, "klee_wait_on", Executor::User);
    return;
  }

  std::uint64_t handle_id = cast<ConstantExpr>(handleExpr)->getZExtValue();
  std::uint64_t wait1Lock = cast<ConstantExpr>(wait1LockExpr)->getZExtValue();
  executor.threadWaitOn(state, handle_id);
  if (wait1Lock > 0) {
    executor.porEventManager.registerCondVarWait1(state, handle_id, wait1Lock);
  }
}

void SpecialFunctionHandler::handleWakeUpWaiting(ExecutionState &state,
                                                 KInstruction *target,
                                                 std::vector<klee::ref<klee::Expr>> &arguments) {
  assert((arguments.size() == 2 || arguments.size() == 3) && "invalid number of arguments to klee_release_waiting");
  ref<Expr> lidExpr = executor.toUnique(state, arguments[0]);
  ref<Expr> modeExpr = executor.toUnique(state, arguments[1]);

  if (!isa<ConstantExpr>(lidExpr) || !isa<ConstantExpr>(modeExpr)) {
    executor.terminateStateOnError(state, "klee_release_waiting", Executor::User);
    return;
  }

  std::uint64_t lid = cast<ConstantExpr>(lidExpr)->getZExtValue();

  // mode == 0 -> KLEE_RELEASE_ALL
  // mode == 1 -> KLEE_RELEASE_SINGLE
  auto mode = cast<ConstantExpr>(modeExpr)->getZExtValue();
  assert((mode == 0 || mode == 1) && "invalid mode given to klee_release_waiting");
  bool releaseSingle = (mode == 1);

  bool registerAsNotificationEvent = false;
  if (arguments.size() > 2) {
    ref<Expr> asEventExpr = executor.toUnique(state, arguments[2]);

    if (!isa<ConstantExpr>(asEventExpr)) {
      executor.terminateStateOnError(state, "klee_release_waiting", Executor::User);
      return;
    }

    auto asEvent = static_cast<por_event_t>(cast<ConstantExpr>(asEventExpr)->getZExtValue());

    // only allow por_broadcast and por_signal with corresponding mode
    bool legitimateRegistration = (asEvent == por_broadcast && !releaseSingle) || (asEvent == por_signal && releaseSingle);
    if (!legitimateRegistration) {
      executor.terminateStateOnError(state, "klee_release_waiting", Executor::User);
      return;
    }
    registerAsNotificationEvent = true;
  }

  executor.threadWakeUpWaiting(state, lid, releaseSingle, registerAsNotificationEvent);
  if (registerAsNotificationEvent && state.lostNotifications >= 3) {
      executor.terminateStateOnError(state, "Three lost notifications, terminating to avoid infinite loop", Executor::Unhandled);
  }
}

void SpecialFunctionHandler::handlePorRegisterEvent(klee::ExecutionState &state,
                                                    klee::KInstruction *target,
                                                    std::vector<klee::ref<klee::Expr>> &arguments) {
  assert(!arguments.empty() && "invalid number of arguments to klee_por_register_event");

  ref<Expr> kindExpr = executor.toUnique(state, arguments[0]);

  if (!isa<ConstantExpr>(kindExpr)) {
    executor.terminateStateOnError(state, "klee_por_register_event", Executor::User);
    return;
  }

  auto kind = (por_event_t) cast<ConstantExpr>(kindExpr)->getZExtValue();

  // All arguments have to be of type uint64_t
  std::vector<std::uint64_t> args;
  for (auto it = arguments.begin() + 1; it != arguments.end(); it++) {
    ref<Expr> expr = executor.toUnique(state, *it);

    if (!isa<ConstantExpr>(expr)) {
      executor.terminateStateOnError(state, "klee_por_register_event", Executor::User);
      return;
    }

    args.push_back(cast<ConstantExpr>(expr)->getZExtValue());
  }

  bool successful = false;
  switch (kind) {
    case por_lock_create:
      successful = executor.porEventManager.registerLockCreate(state, args[0]);
      break;

    case por_lock_destroy:
      successful = executor.porEventManager.registerLockDestroy(state, args[0]);
      break;

    case por_lock_release:
      successful = executor.porEventManager.registerLockRelease(state, args[0]);
      break;

    case por_lock_acquire:
      successful = executor.porEventManager.registerLockAcquire(state, args[0]);
      break;

    case por_condition_variable_create:
      successful = executor.porEventManager.registerCondVarCreate(state, args[0]);
      break;

    case por_condition_variable_destroy:
      successful = executor.porEventManager.registerCondVarDestroy(state, args[0]);
      break;

    case por_wait1:
      successful = executor.porEventManager.registerCondVarWait1(state, args[0], args[1]);
      break;

    case por_wait2:
      successful = executor.porEventManager.registerCondVarWait2(state, args[0], args[1]);
      break;

    default:
      executor.terminateStateOnError(state, "klee_por_register_event", Executor::User,
              nullptr, "Invalid por event kind specified");
      break;
  }

  if (!successful) {
    executor.terminateStateOnError(state, "klee_por_register_event", Executor::User);
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

  ThreadId tid;
  bool found = false;

  // For now we assume that the runtime struct ptr is unique for every pthread object in the runtime.
  // (At the current time, this is guaranteed with the pthread implementation)
  for (const auto& th : state.threads) {
    if (th.second.runtimeStructPtr == expr) {
      tid = th.first;
      found = true;
      break;
    }
  }

  if (!found) {
    executor.terminateStateOnError(state, "klee_por_thread_join", Executor::User);
    return;
  }

  if (!executor.porEventManager.registerThreadJoin(state, tid)) {
    executor.terminateStateOnError(state, "klee_por_thread_join", Executor::User);
  }
}

void SpecialFunctionHandler::handlePorThreadExit(ExecutionState &state,
                                                 KInstruction *target,
                                                 std::vector<klee::ref<klee::Expr>> &arguments) {
  assert(arguments.empty() && "invalid number of arguments to klee_por_thread_exit");

  if (!executor.porEventManager.registerThreadExit(state, state.currentThreadId())) {
    executor.terminateStateOnError(state, "klee_por_register_event", Executor::User);
  }
}

void SpecialFunctionHandler::handlePuts(klee::ExecutionState &state,
                                        klee::KInstruction *target,
                                        std::vector<klee::ref<klee::Expr>> &arguments) {
  assert(arguments.size() == 1 && "invalid number of arguments to puts");

  llvm::outs() << readStringAtAddress(state, arguments[0]) << "\n";
  llvm::outs().flush();

  executor.bindLocal(target, state, ConstantExpr::create(0, Expr::Int32));
}
