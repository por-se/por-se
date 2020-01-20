//===-- Executor.cpp ------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Executor.h"

#include "../Expr/ArrayExprOptimizer.h"
#include "Context.h"
#include "CoreStats.h"
#include "ExternalDispatcher.h"
#include "ImpliedValue.h"
#include "Memory.h"
#include "MemoryManager.h"
#include "MemoryState.h"
#include "PTree.h"
#include "Searcher.h"
#include "SeedInfo.h"
#include "SpecialFunctionHandler.h"
#include "StatsTracker.h"
#include "TimingSolver.h"
#include "UserSearcher.h"

#include "klee/Common.h"
#include "klee/Config/Version.h"
#include "klee/ExecutionState.h"
#include "klee/Expr/Assignment.h"
#include "klee/Expr/Expr.h"
#include "klee/Expr/ExprPPrinter.h"
#include "klee/Expr/ExprSMTLIBPrinter.h"
#include "klee/Expr/ExprUtil.h"
#include "klee/Internal/ADT/KTest.h"
#include "klee/Internal/ADT/RNG.h"
#include "klee/Internal/Module/Cell.h"
#include "klee/Internal/Module/InstructionInfoTable.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/KModule.h"
#include "klee/Internal/Support/ErrorHandling.h"
#include "klee/Internal/Support/FileHandling.h"
#include "klee/Internal/Support/FloatEvaluation.h"
#include "klee/Internal/Support/ModuleUtil.h"
#include "klee/Internal/System/MemoryUsage.h"
#include "klee/Internal/System/Time.h"
#include "klee/Interpreter.h"
#include "klee/OptionCategories.h"
#include "klee/Solver/SolverCmdLine.h"
#include "klee/Solver/SolverStats.h"
#include "klee/StatePruningCmdLine.h"
#include "klee/TimerStatIncrementer.h"
#include "klee/util/GetElementPtrTypeIterator.h"

#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/raw_ostream.h"

#include "por/node.h"
#include "por/configuration.h"
#include "por/csd.h"

#include "RaceDetection/DataRaceDetection.h"
#include "RaceDetection/StateBoundTimingSolver.h"

#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cxxabi.h>
#include <fstream>
#include <iomanip>
#include <iosfwd>
#include <memory>
#include <sstream>
#include <string>
#include <sys/mman.h>
#include <variant>
#include <vector>


using namespace llvm;
using namespace klee;

namespace klee {
cl::OptionCategory DebugCat("Debugging options",
                            "These are debugging options.");

cl::OptionCategory ExtCallsCat("External call policy options",
                               "These options impact external calls.");

cl::OptionCategory SeedingCat(
    "Seeding options",
    "These options are related to the use of seeds to start exploration.");

cl::OptionCategory
    TerminationCat("State and overall termination options",
                   "These options control termination of the overall KLEE "
                   "execution and of individual states.");

cl::OptionCategory TestGenCat("Test generation options",
                              "These options impact test generation.");

cl::opt<std::string> MaxTime(
    "max-time",
    cl::desc("Halt execution after the specified duration.  "
             "Set to 0s to disable (default=0s)"),
    cl::init("0s"),
    cl::cat(TerminationCat));
} // namespace klee

namespace {
  cl::opt<bool> LogStateJSON(
      "log-state-json-files",
      cl::desc("Creates two files (states.json, states_fork.json) in output directory that record relevant information about states (default=false)"),
      cl::init(false));

#ifdef HAVE_ZLIB_H
  cl::opt<bool> CompressLogStateJSON(
      "compress-log-state-json-files",
      cl::desc("Compress the files created by -log-state-json-files in gzip format."),
      cl::init(false));
#endif

/*** Test generation options ***/

cl::opt<bool> DumpStatesOnHalt(
    "dump-states-on-halt",
    cl::init(true),
    cl::desc("Dump test cases for all active states on exit (default=true)"),
    cl::cat(TestGenCat));

cl::opt<bool> OnlyOutputStatesCoveringNew(
    "only-output-states-covering-new",
    cl::init(false),
    cl::desc("Only output test cases covering new code (default=false)"),
    cl::cat(TestGenCat));

cl::opt<bool> EmitAllErrors(
    "emit-all-errors", cl::init(true),
    cl::desc("Generate tests cases for all errors "
             "(default=true, i.e. one per (error,instruction) pair)"),
    cl::cat(TestGenCat));

cl::opt<bool> DumpThreadSegmentsConfiguration(
    "dump-thread-segments",
    cl::init(true),
    cl::desc("Ouput the heap and stack memory regions of each created thread (default=true)"),
    cl::cat(TestGenCat));

/* Constraint solving options */

cl::opt<unsigned> MaxSymArraySize(
    "max-sym-array-size",
    cl::desc(
        "If a symbolic array exceeds this size (in bytes), symbolic addresses "
        "into this array are concretized.  Set to 0 to disable (default=0)"),
    cl::init(0),
    cl::cat(SolvingCat));

cl::opt<bool>
    SimplifySymIndices("simplify-sym-indices",
                       cl::init(false),
                       cl::desc("Simplify symbolic accesses using equalities "
                                "from other constraints (default=false)"),
                       cl::cat(SolvingCat));

cl::opt<bool>
    EqualitySubstitution("equality-substitution", cl::init(true),
                         cl::desc("Simplify equality expressions before "
                                  "querying the solver (default=true)"),
                         cl::cat(SolvingCat));


/*** External call policy options ***/

enum class ExternalCallPolicy {
  None,     // No external calls allowed
  Concrete, // Only external calls with concrete arguments allowed
  All,      // All external calls allowed
};

cl::opt<ExternalCallPolicy> ExternalCalls(
    "external-calls",
    cl::desc("Specify the external call policy"),
    cl::values(
        clEnumValN(
            ExternalCallPolicy::None, "none",
            "No external function calls are allowed."),
        clEnumValN(ExternalCallPolicy::Concrete, "concrete",
                   "Only external function calls with concrete arguments are "
                   "allowed (default)"),
        clEnumValN(ExternalCallPolicy::All, "all",
                   "All external function calls are allowed.  This concretizes "
                   "any symbolic arguments in calls to external functions.")
            KLEE_LLVM_CL_VAL_END),
    cl::init(ExternalCallPolicy::Concrete),
    cl::cat(ExtCallsCat));

cl::opt<bool> SuppressExternalWarnings(
    "suppress-external-warnings",
    cl::init(false),
    cl::desc("Supress warnings about calling external functions."),
    cl::cat(ExtCallsCat));

cl::opt<bool> AllExternalWarnings(
    "all-external-warnings",
    cl::init(true),
    cl::desc("Issue a warning everytime an external call is made, "
             "as opposed to once per function (default=true)"),
    cl::cat(ExtCallsCat));


/*** Seeding options ***/

cl::opt<bool> AlwaysOutputSeeds(
    "always-output-seeds",
    cl::init(true),
    cl::desc(
        "Dump test cases even if they are driven by seeds only (default=true)"),
    cl::cat(SeedingCat));

cl::opt<bool> OnlyReplaySeeds(
    "only-replay-seeds",
    cl::init(false),
    cl::desc("Discard states that do not have a seed (default=false)."),
    cl::cat(SeedingCat));

cl::opt<bool> OnlySeed("only-seed",
                       cl::init(false),
                       cl::desc("Stop execution after seeding is done without "
                                "doing regular search (default=false)."),
                       cl::cat(SeedingCat));

cl::opt<bool>
    AllowSeedExtension("allow-seed-extension",
                       cl::init(false),
                       cl::desc("Allow extra (unbound) values to become "
                                "symbolic during seeding (default=false)."),
                       cl::cat(SeedingCat));

cl::opt<bool> ZeroSeedExtension(
    "zero-seed-extension",
    cl::init(false),
    cl::desc(
        "Use zero-filled objects if matching seed not found (default=false)"),
    cl::cat(SeedingCat));

cl::opt<bool> AllowSeedTruncation(
    "allow-seed-truncation",
    cl::init(false),
    cl::desc("Allow smaller buffers than in seeds (default=false)."),
    cl::cat(SeedingCat));

cl::opt<bool> NamedSeedMatching(
    "named-seed-matching",
    cl::init(false),
    cl::desc("Use names to match symbolic objects to inputs (default=false)."),
    cl::cat(SeedingCat));

cl::opt<std::string>
    SeedTime("seed-time",
             cl::desc("Amount of time to dedicate to seeds, before normal "
                      "search (default=0s (off))"),
             cl::cat(SeedingCat));


/*** Termination criteria options ***/

cl::list<Executor::TerminateReason> ExitOnErrorType(
    "exit-on-error-type",
    cl::desc(
        "Stop execution after reaching a specified condition (default=false)"),
    cl::values(
        clEnumValN(Executor::Abort, "Abort", "The program crashed"),
        clEnumValN(Executor::Assert, "Assert", "An assertion was hit"),
        clEnumValN(Executor::BadVectorAccess, "BadVectorAccess",
                   "Vector accessed out of bounds"),
        clEnumValN(Executor::Exec, "Exec",
                   "Trying to execute an unexpected instruction"),
        clEnumValN(Executor::External, "External",
                   "External objects referenced"),
        clEnumValN(Executor::Free, "Free", "Freeing invalid memory"),
        clEnumValN(Executor::Model, "Model", "Memory model limit hit"),
        clEnumValN(Executor::Overflow, "Overflow", "An overflow occurred"),
        clEnumValN(Executor::Ptr, "Ptr", "Pointer error"),
        clEnumValN(Executor::ReadOnly, "ReadOnly", "Write to read-only memory"),
        clEnumValN(Executor::ReportError, "ReportError",
                   "klee_report_error called"),
        clEnumValN(Executor::User, "User", "Wrong klee_* functions invocation"),
        clEnumValN(Executor::Deadlock, "Deadlock", "At least two threads are in a deadlock state"),
        clEnumValN(Executor::UnsafeMemoryAccess, "UnsafeMemoryAccess", "A data race was detected"),
        clEnumValN(Executor::Unhandled, "Unhandled",
                   "Unhandled instruction hit") KLEE_LLVM_CL_VAL_END),
    cl::ZeroOrMore,
    cl::cat(TerminationCat));

cl::opt<unsigned long long> MaxInstructions(
    "max-instructions",
    cl::desc("Stop execution after this many instructions.  Set to 0 to disable (default=0)"),
    cl::init(0),
    cl::cat(TerminationCat));

cl::opt<unsigned>
    MaxForks("max-forks",
             cl::desc("Only fork this many times.  Set to -1 to disable (default=-1)"),
             cl::init(~0u),
             cl::cat(TerminationCat));

cl::opt<unsigned> MaxDepth(
    "max-depth",
    cl::desc("Only allow this many symbolic branches.  Set to 0 to disable (default=0)"),
    cl::init(0),
    cl::cat(TerminationCat));

cl::opt<unsigned> MaxMemory("max-memory",
                            cl::desc("Refuse to fork when above this amount of "
#ifdef ENABLE_VERIFIED_FINGERPRINTS
                                     "memory (in MB) (default=50000)"),
                            cl::init(50000),
#else
                                     "memory (in MB) (default=2000)"),
                            cl::init(2000),
#endif
                            cl::cat(TerminationCat));

cl::opt<bool> MaxMemoryInhibit(
    "max-memory-inhibit",
    cl::desc(
        "Inhibit forking at memory cap (vs. random terminate) (default=true)"),
    cl::init(true),
    cl::cat(TerminationCat));

  cl::opt<bool> ExitOnMaxMemory(
    "exit-on-max-memory",
    cl::desc(
        "Instead of killing states or inhibiting forking, exit KLEE once memory cap was hit (default=false)"),
    cl::init(false),
    cl::cat(TerminationCat));


cl::opt<unsigned> RuntimeMaxStackFrames(
    "max-stack-frames",
    cl::desc("Terminate a state after this many stack frames.  Set to 0 to "
             "disable (default=8192)"),
    cl::init(8192),
    cl::cat(TerminationCat));

cl::opt<double> MaxStaticForkPct(
    "max-static-fork-pct", cl::init(1.),
    cl::desc("Maximum percentage spent by an instruction forking out of the "
             "forking of all instructions (default=1.0 (always))"),
    cl::cat(TerminationCat));

cl::opt<double> MaxStaticSolvePct(
    "max-static-solve-pct", cl::init(1.),
    cl::desc("Maximum percentage of solving time that can be spent by a single "
             "instruction over total solving time for all instructions "
             "(default=1.0 (always))"),
    cl::cat(TerminationCat));

cl::opt<double> MaxStaticCPForkPct(
    "max-static-cpfork-pct", cl::init(1.),
    cl::desc("Maximum percentage spent by an instruction of a call path "
             "forking out of the forking of all instructions in the call path "
             "(default=1.0 (always))"),
    cl::cat(TerminationCat));

cl::opt<double> MaxStaticCPSolvePct(
    "max-static-cpsolve-pct", cl::init(1.),
    cl::desc("Maximum percentage of solving time that can be spent by a single "
             "instruction of a call path over total solving time for all "
             "instructions (default=1.0 (always))"),
    cl::cat(TerminationCat));

cl::opt<std::string> TimerInterval(
    "timer-interval",
    cl::desc("Minimum interval to check timers. "
             "Affects -max-time, -istats-write-interval, -stats-write-interval, and -uncovered-update-interval (default=1s)"),
    cl::init("1s"),
    cl::cat(TerminationCat));


/*** Debugging options ***/

llvm::cl::opt<bool> DebugPrintCalls("debug-print-calls", cl::init(false), cl::cat(DebugCat));

llvm::cl::opt<bool> DebugPrintPorStats("debug-print-por-statistics", cl::init(false), cl::cat(DebugCat));

cl::opt<bool> EnableDataRaceDetection("data-race-detection",
    cl::init(true),
    cl::desc("Check memory accesses for races between threads"));

/// The different query logging solvers that can switched on/off
enum PrintDebugInstructionsType {
  STDERR_ALL, ///
  STDERR_SRC,
  STDERR_COMPACT,
  FILE_ALL,    ///
  FILE_SRC,    ///
  FILE_COMPACT ///
};

llvm::cl::bits<PrintDebugInstructionsType> DebugPrintInstructions(
    "debug-print-instructions",
    llvm::cl::desc("Log instructions during execution."),
    llvm::cl::values(
        clEnumValN(STDERR_ALL, "all:stderr",
                   "Log all instructions to stderr "
                   "in format [src, inst_id, "
                   "llvm_inst]"),
        clEnumValN(STDERR_SRC, "src:stderr",
                   "Log all instructions to stderr in format [src, inst_id]"),
        clEnumValN(STDERR_COMPACT, "compact:stderr",
                   "Log all instructions to stderr in format [inst_id]"),
        clEnumValN(FILE_ALL, "all:file",
                   "Log all instructions to file "
                   "instructions.txt in format [src, "
                   "inst_id, llvm_inst]"),
        clEnumValN(FILE_SRC, "src:file",
                   "Log all instructions to file "
                   "instructions.txt in format [src, "
                   "inst_id]"),
        clEnumValN(FILE_COMPACT, "compact:file",
                   "Log all instructions to file instructions.txt in format "
                   "[inst_id]") KLEE_LLVM_CL_VAL_END),
    llvm::cl::CommaSeparated,
    cl::cat(DebugCat));

#ifdef HAVE_ZLIB_H
cl::opt<bool> DebugCompressInstructions(
    "debug-compress-instructions", cl::init(false),
    cl::desc(
        "Compress the logged instructions in gzip format (default=false)."),
    cl::cat(DebugCat));
#endif

cl::opt<bool> DebugCheckForImpliedValues(
    "debug-check-for-implied-values", cl::init(false),
    cl::desc("Debug the implied value optimization"),
    cl::cat(DebugCat));

cl::opt<bool>ExploreSchedules(
      "explore-schedules",
      cl::desc("Explore alternative thread schedules (default=true)"),
      cl::init(true));

cl::opt<bool> DebugAlternatives(
  "debug-alternative-schedules",
  cl::init(false),
  cl::cat(DebugCat));

enum class ThreadSchedulingPolicy {
  First, // first runnable thread (by id)
  Last, // last runnable thread (by id)
  RoundRobin, // switch threads after each event registration
  Random, // random runnable thread
};

cl::opt<ThreadSchedulingPolicy> ThreadScheduling(
    "thread-scheduling",
    cl::desc("Specify the thread scheduling policy (only applies outside of catch-up phases)"),
    cl::values(
        clEnumValN(
          ThreadSchedulingPolicy::First, "first",
          "Pick the first runnable thread (determined by its id): main thread if runnable, thread with next lowest id otherwise."),
        clEnumValN(
          ThreadSchedulingPolicy::Last, "last",
          "Pick the last runnable thread (determined by its id): most recent runnable thread."),
        clEnumValN(
          ThreadSchedulingPolicy::RoundRobin, "round-robin",
          "Picks runnable threads in a determined order, changes on event registration."),
        clEnumValN(
          ThreadSchedulingPolicy::Random, "random",
          "Pick a random thread (default).")
        KLEE_LLVM_CL_VAL_END),
    cl::init(ThreadSchedulingPolicy::Random));
} // namespace

namespace klee {
  RNG theRNG;
}

// XXX hack
extern "C" unsigned dumpStates, dumpPTree;
unsigned dumpStates = 0, dumpPTree = 0;

const char *Executor::TerminateReasonNames[] = {
  [ Abort ] = "abort",
  [ Assert ] = "assert",
  [ BadVectorAccess ] = "bad_vector_access",
  [ Exec ] = "exec",
  [ External ] = "external",
  [ Free ] = "free",
  [ Model ] = "model",
  [ Overflow ] = "overflow",
  [ Ptr ] = "ptr",
  [ ReadOnly ] = "readonly",
  [ ReportError ] = "reporterror",
  [ User ] = "user",
  [ Deadlock ] = "deadlock",
  [ UnsafeMemoryAccess ] = "unsafememoryaccess",
  [ Unhandled ] = "xxx",
};


Executor::Executor(LLVMContext &ctx, const InterpreterOptions &opts,
                   InterpreterHandler *ih)
    : Interpreter(opts), interpreterHandler(ih), searcher(0),
      externalDispatcher(new ExternalDispatcher(ctx)), statsTracker(0),
      pathWriter(0), symPathWriter(0), specialFunctionHandler(0), timers{time::Span(TimerInterval)},
      replayKTest(0), replayPath(0), usingSeeds(0),
      atMemoryLimit(false), inhibitForking(false), haltExecution(false),
      ivcEnabled(false), debugLogBuffer(debugBufferString),
      executorStartTime(std::chrono::steady_clock::now()) {

  const time::Span maxTime{MaxTime};
  if (maxTime) timers.add(
        std::make_unique<Timer>(maxTime, [&]{
        klee_message("HaltTimer invoked");
        setHaltExecution(true);
      }));

  coreSolverTimeout = time::Span{MaxCoreSolverTime};
  if (coreSolverTimeout) UseForkedCoreSolver = true;
  Solver *coreSolver = klee::createCoreSolver(CoreSolverToUse);
  if (!coreSolver) {
    klee_error("Failed to create core solver\n");
  }

  Solver *solver = constructSolverChain(
      coreSolver,
      interpreterHandler->getOutputFilename(ALL_QUERIES_SMT2_FILE_NAME),
      interpreterHandler->getOutputFilename(SOLVER_QUERIES_SMT2_FILE_NAME),
      interpreterHandler->getOutputFilename(ALL_QUERIES_KQUERY_FILE_NAME),
      interpreterHandler->getOutputFilename(SOLVER_QUERIES_KQUERY_FILE_NAME));

  this->solver = new TimingSolver(solver, EqualitySubstitution);
  memory = new MemoryManager(&arrayCache);

  if (DumpThreadSegmentsConfiguration) {
    auto thSegmentsFile = interpreterHandler->openOutputFile("thread-segments.conf");
    if (thSegmentsFile) {
      memory->outputConfig(std::move(thSegmentsFile));
    }
  }

  initializeSearchOptions();

  if (OnlyOutputStatesCoveringNew && !StatsTracker::useIStats())
    klee_error("To use --only-output-states-covering-new, you need to enable --output-istats.");

  if (DebugPrintInstructions.isSet(FILE_ALL) ||
      DebugPrintInstructions.isSet(FILE_COMPACT) ||
      DebugPrintInstructions.isSet(FILE_SRC)) {
    std::string debug_file_name =
        interpreterHandler->getOutputFilename("instructions.txt");
    std::string error;
#ifdef HAVE_ZLIB_H
    if (!DebugCompressInstructions) {
#endif
      debugInstFile = klee_open_output_file(debug_file_name, error);
#ifdef HAVE_ZLIB_H
    } else {
      debug_file_name.append(".gz");
      debugInstFile = klee_open_compressed_output_file(debug_file_name, error);
    }
#endif
    if (!debugInstFile) {
      klee_error("Could not open file %s : %s", debug_file_name.c_str(),
                 error.c_str());
    }
  }

  if (LogStateJSON) {
    size_t stateLoggingOverhead = util::GetTotalMallocUsage();

    std::string states_file_name =
      interpreterHandler->getOutputFilename("states.json");

    std::string error;
#ifdef HAVE_ZLIB_H
    if (!CompressLogStateJSON) {
#endif
      statesJSONFile = klee_open_output_file(states_file_name, error);
#ifdef HAVE_ZLIB_H
    } else {
      states_file_name.append(".gz");
      statesJSONFile = klee_open_compressed_output_file(states_file_name, error);
    }
#endif

    if (statesJSONFile) {
      (*statesJSONFile) << "[\n";
      (*statesJSONFile) << "  {\n";
      (*statesJSONFile) << "    \"functionpointer_size\": "
                        << sizeof(llvm::Function *) << ",\n";
      (*statesJSONFile) << "    \"memory_state_size\": "
                        << sizeof(MemoryState) << ",\n";
    } else {
      klee_error("Could not open file %s : %s",
                 states_file_name.c_str(),
                 error.c_str());
    }

    std::string fork_file_name =
      interpreterHandler->getOutputFilename("states_fork.json");

    error = "";

#ifdef HAVE_ZLIB_H
    if (!CompressLogStateJSON) {
#endif
      forkJSONFile = klee_open_output_file(fork_file_name, error);
#ifdef HAVE_ZLIB_H
    } else {
      fork_file_name.append(".gz");
      forkJSONFile = klee_open_compressed_output_file(fork_file_name, error);
    }
#endif
    if (!forkJSONFile) {
      klee_error("Could not open file %s : %s",
                 fork_file_name.c_str(),
                 error.c_str());
    }

    stateLoggingOverhead = util::GetTotalMallocUsage() - stateLoggingOverhead;

    if (statesJSONFile) {
      (*statesJSONFile) << "    \"logging_overhead\": "
                        << stateLoggingOverhead << ",\n";
    }
  }
}

llvm::Module *
Executor::setModule(std::vector<std::unique_ptr<llvm::Module>> &modules,
                    const ModuleOptions &opts) {
  assert(!kmodule && !modules.empty() &&
         "can only register one module"); // XXX gross

  kmodule = std::unique_ptr<KModule>(new KModule());

  // Preparing the final module happens in multiple stages

  // Link with KLEE intrinsics library before running any optimizations
  SmallString<128> LibPath(opts.LibraryDir);
  llvm::sys::path::append(LibPath, "libkleeRuntimeIntrinsic.bca");
  std::string error;
  if (!klee::loadFile(LibPath.str(), modules[0]->getContext(), modules,
                      error)) {
    klee_error("Could not load KLEE intrinsic file %s", LibPath.c_str());
  }

  // 1.) Link the modules together
  while (kmodule->link(modules, opts.EntryPoint)) {
    // 2.) Apply different instrumentation
    kmodule->instrument(opts);
  }

  // 3.) Optimise and prepare for KLEE

  // Create a list of functions that should be preserved if used
  std::vector<const char *> preservedFunctions;
  specialFunctionHandler = new SpecialFunctionHandler(*this);
  specialFunctionHandler->prepare(preservedFunctions);

  preservedFunctions.push_back(opts.EntryPoint.c_str());

  // Preserve the free-standing library calls
  preservedFunctions.push_back("memset");
  preservedFunctions.push_back("memcpy");
  preservedFunctions.push_back("memcmp");
  preservedFunctions.push_back("memmove");

  kmodule->optimiseAndPrepare(opts, preservedFunctions);
  kmodule->checkModule();

  // 4.) Manifest the module
  kmodule->manifest(interpreterHandler, StatsTracker::useStatistics());

  specialFunctionHandler->bind();

  if (StatsTracker::useStatistics() || userSearcherRequiresMD2U()) {
    statsTracker = 
      new StatsTracker(*this,
                       interpreterHandler->getOutputFilename("assembly.ll"),
                       userSearcherRequiresMD2U());
  }

  // Initialize the context.
  DataLayout *TD = kmodule->targetData.get();
  Context::initialize(TD->isLittleEndian(),
                      (Expr::Width)TD->getPointerSizeInBits());

  if (PruneStates)
    MemoryState::setKModule(kmodule.get());

  return kmodule->module.get();
}

Executor::~Executor() {
  delete memory;
  delete externalDispatcher;
  delete specialFunctionHandler;
  delete statsTracker;
  delete solver;
  if (statesJSONFile) {
    (*statesJSONFile) << "\n]\n";
  }
  if (forkJSONFile) {
    (*forkJSONFile) << "\n]\n";
  }
}

/***/

void Executor::initializeGlobalObject(ExecutionState &state, ObjectState *os,
                                      const Constant *c, unsigned offset,
                                      const ThreadId &byTid) {
  const auto targetData = kmodule->targetData.get();
  if (const ConstantVector *cp = dyn_cast<ConstantVector>(c)) {
    unsigned elementSize =
      targetData->getTypeStoreSize(cp->getType()->getElementType());
    for (unsigned i=0, e=cp->getNumOperands(); i != e; ++i)
      initializeGlobalObject(state, os, cp->getOperand(i),
                             offset + i * elementSize, byTid);
  } else if (isa<ConstantAggregateZero>(c)) {
    unsigned i, size = targetData->getTypeStoreSize(c->getType());
    for (i=0; i<size; i++)
      os->write8(offset+i, (uint8_t) 0);
    if (PruneStates) {
      const MemoryObject *mo = os->getObject();
      ref<ConstantExpr> address = mo->getBaseExpr();
      address = address->Add(ConstantExpr::alloc(offset, Expr::Int64));
      state.memoryState.registerWrite(address, *mo, *os, size);
    }
  } else if (const ConstantArray *ca = dyn_cast<ConstantArray>(c)) {
    unsigned elementSize =
      targetData->getTypeStoreSize(ca->getType()->getElementType());
    for (unsigned i=0, e=ca->getNumOperands(); i != e; ++i)
      initializeGlobalObject(state, os, ca->getOperand(i),
                             offset + i * elementSize, byTid);
  } else if (const ConstantStruct *cs = dyn_cast<ConstantStruct>(c)) {
    const StructLayout *sl =
      targetData->getStructLayout(cast<StructType>(cs->getType()));
    for (unsigned i=0, e=cs->getNumOperands(); i != e; ++i)
      initializeGlobalObject(state, os, cs->getOperand(i),
                             offset + sl->getElementOffset(i), byTid);
  } else if (const ConstantDataSequential *cds =
               dyn_cast<ConstantDataSequential>(c)) {
    unsigned elementSize =
      targetData->getTypeStoreSize(cds->getElementType());
    for (unsigned i=0, e=cds->getNumElements(); i != e; ++i)
      initializeGlobalObject(state, os, cds->getElementAsConstant(i),
                             offset + i * elementSize, byTid);
  } else if (!isa<UndefValue>(c) && !isa<MetadataAsValue>(c)) {
    unsigned StoreBits = targetData->getTypeStoreSizeInBits(c->getType());
    ref<ConstantExpr> C = evalConstant(c, byTid, nullptr);

    // Extend the constant if necessary;
    assert(StoreBits >= C->getWidth() && "Invalid store size!");
    if (StoreBits > C->getWidth())
      C = C->ZExt(StoreBits);

    os->write(offset, C);
    if (PruneStates) {
      const MemoryObject *mo = os->getObject();
      ref<ConstantExpr> address = mo->getBaseExpr();
      address = address->Add(ConstantExpr::alloc(offset, Expr::Int64));
      state.memoryState.registerWrite(address, *mo, *os, StoreBits / 8);
    }
  } else {
    assert(isa<UndefValue>(c));
    std::size_t num = getWidthForLLVMType(c->getType()) / 8;
    for (std::size_t i = 0; i < num; ++i)
      os->write8(offset + i, 0xAB); // like ObjectState::initializeToRandom()

    if (PruneStates) {
      const MemoryObject *mo = os->getObject();
      ref<ConstantExpr> address = mo->getBaseExpr();
      address = address->Add(ConstantExpr::alloc(offset, Expr::Int64));
      state.memoryState.registerWrite(address, *mo, *os, num);
    }
  }
}

MemoryObject * Executor::addExternalObject(ExecutionState &state, 
                                           void *addr, unsigned size, 
                                           bool isReadOnly) {
  auto mo = memory->allocateFixed(reinterpret_cast<std::uint64_t>(addr),
                                  size, nullptr, state.thread(),
                                  state.stackFrameIndex());
  ObjectState *os = bindObjectInState(state, mo, false);
  for (unsigned i = 0; i < size; i++) {
    os->write8(i, ((uint8_t*)addr)[i]);
  }
  if (PruneStates && !isReadOnly) {
    // NOTE: this assumes addExternalObject is only called for initialization
    state.memoryState.registerWrite(mo->getBaseExpr(), *mo, *os, size);
  }
  if (isReadOnly)
    os->setReadOnly(true);  
  return mo;
}


extern void *__dso_handle __attribute__ ((__weak__));

void Executor::initializeGlobals(ExecutionState &state) {
  Module *m = kmodule->module.get();

  if (!m->getModuleInlineAsm().empty())
    klee_warning("executable has module level assembly (ignoring)");
  // represent function globals using the address of the actual llvm function
  // object. given that we use malloc to allocate memory in states this also
  // ensures that we won't conflict. we don't need to allocate a memory object
  // since reading/writing via a function pointer is unsupported anyway.
  for (auto &i : *m) {
    Function *f = &i;
    ref<ConstantExpr> addr(0);

    // If the symbol has external weak linkage then it is implicitly
    // not defined in this module; if it isn't resolvable then it
    // should be null.
    if (f->hasExternalWeakLinkage() && 
        !externalDispatcher->resolveSymbol(f->getName())) {
      addr = Expr::createPointer(0);
    } else {
      addr = Expr::createPointer(reinterpret_cast<std::uint64_t>(f));
      legalFunctions.insert(reinterpret_cast<std::uint64_t>(f));
    }

    memory->registerFunction(f, addr);
  }

#ifndef WINDOWS
  int *errno_addr = getErrnoLocation(state);
  *errno_addr = 0;
  MemoryObject *errnoObj =
      addExternalObject(state, (void *)errno_addr, sizeof *errno_addr, false);
  // Copy values from and to program space explicitly
  errnoObj->isUserSpecified = true;

  // Should be the main thread
  state.thread().errnoMo = errnoObj;
#endif

  // Disabled, we don't want to promote use of live externals.
#ifdef HAVE_CTYPE_EXTERNALS
#ifndef WINDOWS
#ifndef DARWIN
  /* from /usr/include/ctype.h:
       These point into arrays of 384, so they can be indexed by any `unsigned
       char' value [0,255]; by EOF (-1); or by any `signed char' value
       [-128,-1).  ISO C requires that the ctype functions work for `unsigned */
  const uint16_t **addr = __ctype_b_loc();
  addExternalObject(state, const_cast<uint16_t*>(*addr-128),
                    384 * sizeof **addr, true);
  addExternalObject(state, addr, sizeof(*addr), true);
    
  const int32_t **lower_addr = __ctype_tolower_loc();
  addExternalObject(state, const_cast<int32_t*>(*lower_addr-128),
                    384 * sizeof **lower_addr, true);
  addExternalObject(state, lower_addr, sizeof(*lower_addr), true);
  
  const int32_t **upper_addr = __ctype_toupper_loc();
  addExternalObject(state, const_cast<int32_t*>(*upper_addr-128),
                    384 * sizeof **upper_addr, true);
  addExternalObject(state, upper_addr, sizeof(*upper_addr), true);
#endif
#endif
#endif

  // allocate and initialize globals, done in two passes since we may
  // need address of a global in order to initialize some other one.

  // allocate memory objects for all globals
  for (Module::const_global_iterator i = m->global_begin(),
         e = m->global_end();
       i != e; ++i) {
    const GlobalVariable *v = &*i;
    auto globalObjectAlignment = getAllocationAlignment(v);

    if (i->isDeclaration()) {
      // FIXME: We have no general way of handling unknown external
      // symbols. If we really cared about making external stuff work
      // better we could support user definition, or use the EXE style
      // hack where we check the object file information.

      Type *ty = i->getType()->getElementType();
      uint64_t size = 0;
      if (ty->isSized()) {
	size = kmodule->targetData->getTypeStoreSize(ty);
      } else {
        klee_warning("Type for %.*s is not sized", (int)i->getName().size(),
			i->getName().data());
      }

      // XXX - DWD - hardcode some things until we decide how to fix.
#ifndef WINDOWS
      if (i->getName() == "_ZTVN10__cxxabiv117__class_type_infoE") {
        size = 0x2C;
      } else if (i->getName() == "_ZTVN10__cxxabiv120__si_class_type_infoE") {
        size = 0x2C;
      } else if (i->getName() == "_ZTVN10__cxxabiv121__vmi_class_type_infoE") {
        size = 0x2C;
      }
#endif

      if (size == 0) {
        klee_warning("Unable to find size for global variable: %.*s (use will result in out of bounds access)",
			(int)i->getName().size(), i->getName().data());
      }

      auto *mo = memory->registerGlobalData(v, size, globalObjectAlignment);
      ObjectState *os = bindObjectInState(state, mo, false);

      // Program already running = object already initialized.  Read
      // concrete value and write it to our copy.
      if (size) {
        void *addr;
        if (i->getName() == "__dso_handle") {
          addr = &__dso_handle; // wtf ?
        } else {
          addr = externalDispatcher->resolveSymbol(i->getName());
        }
        if (!addr)
          klee_error("unable to load symbol(%s) while initializing globals.", 
                     i->getName().data());

        for (unsigned offset=0; offset<mo->size; offset++) {
          os->write8(offset, ((unsigned char*)addr)[offset]);
        }
        if (PruneStates) {
          state.memoryState.registerWrite(*mo, *os);
        }
      }
    } else {
      Type *ty = i->getType()->getElementType();
      uint64_t size = kmodule->targetData->getTypeStoreSize(ty);

      auto mo = memory->registerGlobalData(v, size, globalObjectAlignment);

      if (!mo)
        llvm::report_fatal_error("out of memory");

      ObjectState *os = bindObjectInState(state, mo, false);

      if (!i->hasInitializer()) {
        os->initializeToRandom();
        if (PruneStates) {
          state.memoryState.registerWrite(*mo, *os);
        }
      }
    }
  }
  
  // link aliases to their definitions (if bound)
  for (auto i = m->alias_begin(), ie = m->alias_end(); i != ie; ++i) {
    // Map the alias to its aliasee's address. This works because we have
    // addresses for everything, even undefined functions.

    // Alias may refer to other alias, not necessarily known at this point.
    // Thus, resolve to real alias directly.
    const GlobalAlias *alias = &*i;
    while (const auto *ga = dyn_cast<GlobalAlias>(alias->getAliasee())) {
      assert(ga != alias && "alias pointing to itself");
      alias = ga;
    }

    memory->registerAlias(&*i, evalConstant(alias->getAliasee(), state.tid()));
  }

  // once all objects are allocated, do the actual initialization
  // remember constant objects to initialise their counter part for external
  // calls
  std::vector<ObjectState *> constantObjects;
  for (Module::const_global_iterator i = m->global_begin(),
         e = m->global_end();
       i != e; ++i) {
    if (i->hasInitializer()) {
      const GlobalVariable *v = &*i;

      auto mo = memory->lookupGlobalMemoryObject(v, state.tid());

      const ObjectState *os = state.addressSpace.findObject(mo);
      assert(os);
      ObjectState *wos = state.addressSpace.getWriteable(mo, os);
      initializeGlobalObject(state, wos, i->getInitializer(), 0, state.tid());
      if (i->isConstant())
        constantObjects.emplace_back(wos);
    }
  }

  // initialise constant memory that is potentially used with external calls
  if (!constantObjects.empty()) {
    // initialise the actual memory with constant values
    state.addressSpace.copyOutConcretes();

    // mark constant objects as read-only
    for (auto obj : constantObjects)
      obj->setReadOnly(true);
  }
}

void Executor::branch(ExecutionState &state,
                      const std::vector<std::pair<std::size_t, ref<Expr>>> &conditions,
                      std::vector<ExecutionState*> &result) {
  TimerStatIncrementer timer(stats::forkTime);
  unsigned N = conditions.size();
  assert(N);

  if (state.needsCatchUp()) {
    auto decision = state.peekDecision();
    bool feasible = false;

    for (auto const &[choice, cond] : conditions) {
      if (choice == decision.branch) {
        feasible = true;
        assert(cond == decision.expr);
        result.push_back(&state);
        state.addConstraint(cond);
        state.addDecision(decision);
      } else {
        result.push_back(nullptr);
      }
    }

    if (!feasible) {
      terminateStateSilently(state);
    }

    return;

  } else if (MaxForks!=~0u && stats::forks >= MaxForks) {
    unsigned next = theRNG.getInt32() % N;
    for (unsigned i=0; i<N; ++i) {
      if (i == next) {
        result.push_back(&state);
      } else {
        result.push_back(NULL);
      }
    }
  } else {
    stats::forks += N-1;

    // XXX do proper balance or keep random?
    result.push_back(&state);
    for (unsigned i=1; i<N; ++i) {
      ExecutionState *es = result[theRNG.getInt32() % i];
      ExecutionState *ns = es->branch();
      addedStates.push_back(ns);
      result.push_back(ns);
      processTree->attach(es->ptreeNode, ns, es);
      updateForkJSON(*es, *ns, *ns);
    }
  }

  // If necessary redistribute seeds to match conditions, killing
  // states if necessary due to OnlyReplaySeeds (inefficient but
  // simple).

  std::map< ExecutionState*, std::vector<SeedInfo> >::iterator it = 
    seedMap.find(&state);
  if (it != seedMap.end()) {
    std::vector<SeedInfo> seeds = it->second;
    seedMap.erase(it);

    // Assume each seed only satisfies one condition (necessarily true
    // when conditions are mutually exclusive and their conjunction is
    // a tautology).
    for (std::vector<SeedInfo>::iterator siit = seeds.begin(), 
           siie = seeds.end(); siit != siie; ++siit) {
      unsigned i;
      for (i=0; i<N; ++i) {
        ref<ConstantExpr> res;
        bool success = 
          solver->getValue(state, siit->assignment.evaluate(conditions[i].second),
                           res);
        assert(success && "FIXME: Unhandled solver failure");
        (void) success;
        if (res->isTrue())
          break;
      }
      
      // If we didn't find a satisfying condition randomly pick one
      // (the seed will be patched).
      if (i==N)
        i = theRNG.getInt32() % N;

      // Extra check in case we're replaying seeds with a max-fork
      if (result[i])
        seedMap[result[i]].push_back(*siit);
    }

    if (OnlyReplaySeeds) {
      for (unsigned i=0; i<N; ++i) {
        if (result[i] && !seedMap.count(result[i])) {
          terminateState(*result[i]);
          result[i] = NULL;
        }
      } 
    }
  }

  for (unsigned i=0; i<N; ++i) {
    if (result[i]) {
      addConstraint(*result[i], conditions[i].second, true);
      result[i]->addDecision(conditions[i].first, conditions[i].second);
    }
  }
}

Executor::StatePair 
Executor::fork(ExecutionState &current, ref<Expr> condition, bool isInternal) {
  Solver::Validity res;
  std::map< ExecutionState*, std::vector<SeedInfo> >::iterator it = 
    seedMap.find(&current);
  bool isSeeding = it != seedMap.end();

  if (!isSeeding && !isa<ConstantExpr>(condition) && 
      (MaxStaticForkPct!=1. || MaxStaticSolvePct != 1. ||
       MaxStaticCPForkPct!=1. || MaxStaticCPSolvePct != 1.) &&
      statsTracker->elapsed() > time::seconds(60)) {
    StatisticManager &sm = *theStatisticManager;

    // FIXME: Just assume that we the call should return the current thread, but what is the correct behavior
    CallPathNode *cpn = current.stackFrame().callPathNode;

    if ((MaxStaticForkPct<1. &&
         sm.getIndexedValue(stats::forks, sm.getIndex()) > 
         stats::forks*MaxStaticForkPct) ||
        (MaxStaticCPForkPct<1. &&
         cpn && (cpn->statistics.getValue(stats::forks) > 
                 stats::forks*MaxStaticCPForkPct)) ||
        (MaxStaticSolvePct<1 &&
         sm.getIndexedValue(stats::solverTime, sm.getIndex()) > 
         stats::solverTime*MaxStaticSolvePct) ||
        (MaxStaticCPForkPct<1. &&
         cpn && (cpn->statistics.getValue(stats::solverTime) > 
                 stats::solverTime*MaxStaticCPSolvePct))) {
      ref<ConstantExpr> value; 
      bool success = solver->getValue(current, condition, value);
      assert(success && "FIXME: Unhandled solver failure");
      (void) success;
      addConstraint(current, EqExpr::create(value, condition));
      condition = value;
    }
  }

  time::Span timeout = coreSolverTimeout;
  if (isSeeding)
    timeout *= static_cast<unsigned>(it->second.size());
  solver->setTimeout(timeout);
  bool success = solver->evaluate(current, condition, res);
  solver->setTimeout(time::Span());
  if (!success) {
    // Since we were unsuccessful, restore the previous program counter for the current thread
    Thread &thread = current.thread();
    thread.pc = thread.prevPc;

    terminateStateEarly(current, "Query timed out (fork).");
    return StatePair(0, 0);
  }

  if (!isSeeding) {
    if (replayPath && !isInternal) {
      assert(replayPosition<replayPath->size() &&
             "ran out of branches in replay path mode");
      bool branch = (*replayPath)[replayPosition++];
      
      if (res==Solver::True) {
        assert(branch && "hit invalid branch in replay path mode");
      } else if (res==Solver::False) {
        assert(!branch && "hit invalid branch in replay path mode");
      } else {
        // add constraints
        if (branch) {
          res = Solver::True;
          addConstraint(current, condition);
        } else  {
          res = Solver::False;
          addConstraint(current, Expr::createIsZero(condition));
        }
      }
    } else if (res==Solver::Unknown) {
      assert(!replayKTest && "in replay mode, only one branch can be true.");
      
      if ((MaxMemoryInhibit && atMemoryLimit) || 
          current.forkDisabled ||
          inhibitForking || 
          (MaxForks!=~0u && stats::forks >= MaxForks)) {

	if (MaxMemoryInhibit && atMemoryLimit)
	  klee_warning_once(0, "skipping fork (memory cap exceeded)");
	else if (current.forkDisabled)
	  klee_warning_once(0, "skipping fork (fork disabled on current path)");
	else if (inhibitForking)
	  klee_warning_once(0, "skipping fork (fork disabled globally)");
	else 
	  klee_warning_once(0, "skipping fork (max-forks reached)");

        TimerStatIncrementer timer(stats::forkTime);
        if (theRNG.getBool()) {
          addConstraint(current, condition);
          res = Solver::True;        
        } else {
          addConstraint(current, Expr::createIsZero(condition));
          res = Solver::False;
        }
      }
    }
  }

  // Fix branch in only-replay-seed mode, if we don't have both true
  // and false seeds.
  if (isSeeding && 
      (current.forkDisabled || OnlyReplaySeeds) && 
      res == Solver::Unknown) {
    bool trueSeed=false, falseSeed=false;
    // Is seed extension still ok here?
    for (std::vector<SeedInfo>::iterator siit = it->second.begin(), 
           siie = it->second.end(); siit != siie; ++siit) {
      ref<ConstantExpr> res;
      bool success = 
        solver->getValue(current, siit->assignment.evaluate(condition), res);
      assert(success && "FIXME: Unhandled solver failure");
      (void) success;
      if (res->isTrue()) {
        trueSeed = true;
      } else {
        falseSeed = true;
      }
      if (trueSeed && falseSeed)
        break;
    }
    if (!(trueSeed && falseSeed)) {
      assert(trueSeed || falseSeed);
      
      res = trueSeed ? Solver::True : Solver::False;
      addConstraint(current, trueSeed ? condition : Expr::createIsZero(condition));
    }
  }


  // XXX - even if the constraint is provable one way or the other we
  // can probably benefit by adding this constraint and allowing it to
  // reduce the other constraints. For example, if we do a binary
  // search on a particular value, and then see a comparison against
  // the value it has been fixed at, we should take this as a nice
  // hint to just use the single constraint instead of all the binary
  // search ones. If that makes sense.
  if (res==Solver::True) {
    if (!isInternal) {
      if (pathWriter) {
        current.pathOS << "1";
      }
    }

    if (!isa<ConstantExpr>(condition)) {
      current.addDecision(1, condition);
    }

    return StatePair(&current, 0);
  } else if (res==Solver::False) {
    if (!isInternal) {
      if (pathWriter) {
        current.pathOS << "0";
      }
    }

    if (!isa<ConstantExpr>(condition)) {
      current.addDecision(0, condition);
    }

    return StatePair(0, &current);
  } else {
    if (current.needsCatchUp()) {
      auto decision = current.peekDecision();

      // add constraints
      if (decision.branch) {
        assert(decision.branch == 1);
        assert(decision.expr == condition);
        current.addConstraint(condition);
        current.addDecision(decision);
        return StatePair(&current, nullptr);
      } else {
        auto invCond = Expr::createIsZero(condition);
        assert(decision.branch == 0);
        assert(decision.expr == condition);
        current.addConstraint(invCond);
        current.addDecision(decision);
        return StatePair(nullptr, &current);
      }
    }

    TimerStatIncrementer timer(stats::forkTime);
    ExecutionState *falseState, *trueState = &current;

    ++stats::forks;

    falseState = trueState->branch();
    addedStates.push_back(falseState);

    updateForkJSON(current, *trueState, *falseState);

    if (it != seedMap.end()) {
      std::vector<SeedInfo> seeds = it->second;
      it->second.clear();
      std::vector<SeedInfo> &trueSeeds = seedMap[trueState];
      std::vector<SeedInfo> &falseSeeds = seedMap[falseState];
      for (std::vector<SeedInfo>::iterator siit = seeds.begin(), 
             siie = seeds.end(); siit != siie; ++siit) {
        ref<ConstantExpr> res;
        bool success = 
          solver->getValue(current, siit->assignment.evaluate(condition), res);
        assert(success && "FIXME: Unhandled solver failure");
        (void) success;
        if (res->isTrue()) {
          trueSeeds.push_back(*siit);
        } else {
          falseSeeds.push_back(*siit);
        }
      }
      
      bool swapInfo = false;
      if (trueSeeds.empty()) {
        if (&current == trueState) swapInfo = true;
        seedMap.erase(trueState);
      }
      if (falseSeeds.empty()) {
        if (&current == falseState) swapInfo = true;
        seedMap.erase(falseState);
      }
      if (swapInfo) {
        std::swap(trueState->coveredNew, falseState->coveredNew);
        std::swap(trueState->coveredLines, falseState->coveredLines);
      }
    }

    processTree->attach(current.ptreeNode, falseState, trueState);

    if (pathWriter) {
      // Need to update the pathOS.id field of falseState, otherwise the same id
      // is used for both falseState and trueState.
      falseState->pathOS = pathWriter->open(current.pathOS);
      if (!isInternal) {
        trueState->pathOS << "1";
        falseState->pathOS << "0";
      }
    }
    if (symPathWriter) {
      falseState->symPathOS = symPathWriter->open(current.symPathOS);
      if (!isInternal) {
        trueState->symPathOS << "1";
        falseState->symPathOS << "0";
      }
    }

    ref<Expr> invertedCondition = Expr::createIsZero(condition);

    trueState->addDecision(1, condition);
    falseState->addDecision(0, condition);

    addConstraint(*trueState, condition, true);
    addConstraint(*falseState, invertedCondition, true);

    // Kinda gross, do we even really still want this option?
    if (MaxDepth && MaxDepth<=trueState->depth) {
      terminateStateEarly(*trueState, "max-depth exceeded.");
      terminateStateEarly(*falseState, "max-depth exceeded.");
      return StatePair(0, 0);
    }

    return StatePair(trueState, falseState);
  }
}

void Executor::addConstraint(ExecutionState &state, ref<Expr> condition, bool alreadyInPath) {
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(condition)) {
    if (!CE->isTrue())
      llvm::report_fatal_error("attempt to add invalid constraint");
    return;
  }

  // Check to see if this constraint violates seeds.
  std::map< ExecutionState*, std::vector<SeedInfo> >::iterator it = 
    seedMap.find(&state);
  if (it != seedMap.end()) {
    bool warn = false;
    for (std::vector<SeedInfo>::iterator siit = it->second.begin(), 
           siie = it->second.end(); siit != siie; ++siit) {
      bool res;
      bool success = 
        solver->mustBeFalse(state, siit->assignment.evaluate(condition), res);
      assert(success && "FIXME: Unhandled solver failure");
      (void) success;
      if (res) {
        siit->patchSeed(state, condition, solver);
        warn = true;
      }
    }
    if (warn)
      klee_warning("seeds patched for violating constraint"); 
  }

  state.addConstraint(condition);

  if (!alreadyInPath) {
    if (state.needsCatchUp()) {
      auto decision = state.peekDecision();
      assert(decision.branch == 0);
      assert(decision.expr == condition);
    }
    state.addDecision(0, condition);
  }

  if (ivcEnabled)
    doImpliedValueConcretization(state, condition, 
                                 ConstantExpr::alloc(1, Expr::Bool));
}

const Cell& Executor::eval(KInstruction *ki, unsigned index, 
                           ExecutionState &state) {
  assert(index < ki->inst->getNumOperands());
  int vnumber = ki->operands[index];

  if (vnumber == -1) {
    Value *v = nullptr;
    if (isa<CallInst>(ki->inst) || isa<InvokeInst>(ki->inst)) {
      CallSite cs(ki->inst);

      if (index == 0) {
        v = cs.getCalledValue();
      } else {
        v = cs.getArgument(index - 1);
      }
    } else {
      v = ki->inst->getOperand(index);
    }

    assert(isa<Constant>(v) && "Invalid type for ad-hoc constant evaluation");
    auto *c = cast<Constant>(v);

    assert(c->isThreadDependent() && "If a constant is not thread dependent, then the constant should have been folded earlier");

    // instructions is null since we want to mimic the behavior during constant folding
    // see: bindModuleConstants
    auto value = evalConstant(c, state.tid(), nullptr);

    // TODO: This is not really a great way to achieve this ...
    // Since we are returning a reference here we cannot return a stack allocated variable.
    // Every invocation of eval directly extracts the value out of the `Cell` and then ignores
    // the actual cell object. Therefore, we should be safe for now ...
    static Cell hackCell;
    hackCell.value = value;
    return hackCell;
  }

  // Determine if this is a constant or not.
  if (vnumber < 0) {
    unsigned index = -vnumber - 2;
    return kmodule->constantTable[index];
  } else {
    unsigned index = vnumber;
    const StackFrame &sf = state.stackFrame();

    if (sf.locals[index].value.get() == nullptr) {
      klee_warning("Null pointer");
    }

    return sf.locals[index];
  }
}

void Executor::bindLocal(KInstruction *target, ExecutionState &state, 
                         ref<Expr> value) {
  Cell &cell = getDestCell(state, target);
  cell.value = value;
}

void Executor::bindArgument(KFunction *kf, unsigned index, 
                            ExecutionState &state, ref<Expr> value) {
  assert(getArgumentCell(state, kf, index).value.isNull() &&
         "argument has previouly been set!");
  if (PruneStates) {
    // no need to unregister argument (can only be set once within the same stack frame)
    state.memoryState.registerArgument(state.tid(), state.stackFrameIndex(), kf, index, value);
  }
  getArgumentCell(state, kf, index).value = value;
}

ref<Expr> Executor::toUnique(const ExecutionState &state, 
                             ref<Expr> &e) {
  ref<Expr> result = e;

  if (!isa<ConstantExpr>(e)) {
    ref<ConstantExpr> value;
    bool isTrue = false;
    e = optimizer.optimizeExpr(e, true);
    solver->setTimeout(coreSolverTimeout);
    if (solver->getValue(state, e, value)) {
      ref<Expr> cond = EqExpr::create(e, value);
      cond = optimizer.optimizeExpr(cond, false);
      if (solver->mustBeTrue(state, cond, isTrue) && isTrue)
        result = value;
    }
    solver->setTimeout(time::Span());
  }
  
  return result;
}


/* Concretize the given expression, and return a possible constant value. 
   'reason' is just a documentation string stating the reason for concretization. */
ref<klee::ConstantExpr> 
Executor::toConstant(ExecutionState &state, 
                     ref<Expr> e,
                     const char *reason) {
  e = state.constraints.simplifyExpr(e);
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(e))
    return CE;

  ref<ConstantExpr> value;
  bool success = solver->getValue(state, e, value);
  assert(success && "FIXME: Unhandled solver failure");
  (void) success;

  std::string str;
  llvm::raw_string_ostream os(str);

  os << "silently concretizing (reason: " << reason << ") expression " << e
     << " to value " << value << " (" << state.pc()->info->file << ":"
     << state.pc()->info->line << ")";

  if (AllExternalWarnings)
    klee_warning("%s", os.str().c_str());
  else
    klee_warning_once(reason, "%s", os.str().c_str());

  addConstraint(state, EqExpr::create(e, value));
    
  return value;
}

void Executor::executeGetValue(ExecutionState &state,
                               ref<Expr> e,
                               KInstruction *target) {
  e = state.constraints.simplifyExpr(e);
  std::map< ExecutionState*, std::vector<SeedInfo> >::iterator it = 
    seedMap.find(&state);
  if (it==seedMap.end() || isa<ConstantExpr>(e)) {
    ref<ConstantExpr> value;
    e = optimizer.optimizeExpr(e, true);
    bool success = solver->getValue(state, e, value);
    assert(success && "FIXME: Unhandled solver failure");
    (void) success;
    bindLocal(target, state, value);
  } else {
    std::set< ref<Expr> > values;
    for (std::vector<SeedInfo>::iterator siit = it->second.begin(), 
           siie = it->second.end(); siit != siie; ++siit) {
      ref<Expr> cond = siit->assignment.evaluate(e);
      cond = optimizer.optimizeExpr(cond, true);
      ref<ConstantExpr> value;
      bool success = solver->getValue(state, cond, value);
      assert(success && "FIXME: Unhandled solver failure");
      (void) success;
      values.insert(value);
    }

    std::vector<std::pair<std::size_t, ref<Expr>>> conditions;
    std::size_t i = 0;
    for (ref<Expr> expr : values) {
      conditions.emplace_back(i++, EqExpr::create(e, expr));
    }

    std::vector<ExecutionState*> branches;
    branch(state, conditions, branches);
    
    std::vector<ExecutionState*>::iterator bit = branches.begin();
    for (std::set< ref<Expr> >::iterator vit = values.begin(), 
           vie = values.end(); vit != vie; ++vit) {
      ExecutionState *es = *bit;
      if (es)
        bindLocal(target, *es, *vit);
      ++bit;
    }
  }
}

void Executor::printDebugInstructions(ExecutionState &state) {
  // check do not print
  if (DebugPrintInstructions.getBits() == 0)
	  return;

  llvm::raw_ostream *stream = 0;
  if (DebugPrintInstructions.isSet(STDERR_ALL) ||
      DebugPrintInstructions.isSet(STDERR_SRC) ||
      DebugPrintInstructions.isSet(STDERR_COMPACT))
    stream = &llvm::errs();
  else
    stream = &debugLogBuffer;

  if (!DebugPrintInstructions.isSet(STDERR_COMPACT) &&
      !DebugPrintInstructions.isSet(FILE_COMPACT)) {
    auto sid = state.id;
    auto tid = state.tid();
    auto sf = state.stackFrameIndex();
    std::stringstream tmp;
    tmp << "[state: " << std::setw(6) << sid
        << " thread: " << std::setw(2) << tid
        << " sf: " << std::setw(2) << sf << "] ";
    (*stream) << tmp.str() << state.pc()->getSourceLocation() << ":";
  }

  (*stream) << state.pc()->info->assemblyLine;

  if (DebugPrintInstructions.isSet(STDERR_ALL) ||
      DebugPrintInstructions.isSet(FILE_ALL))
    (*stream) << ":" << *(state.pc()->inst);
  (*stream) << "\n";

  if (DebugPrintInstructions.isSet(FILE_ALL) ||
      DebugPrintInstructions.isSet(FILE_COMPACT) ||
      DebugPrintInstructions.isSet(FILE_SRC)) {
    debugLogBuffer.flush();
    (*debugInstFile) << debugLogBuffer.str();
    debugBufferString = "";
  }
}

void Executor::stepInstruction(ExecutionState &state) {
  printDebugInstructions(state);
  if (statsTracker)
    statsTracker->stepInstruction(state);

  if(state.needsCatchUp())
    ++stats::catchUpInstructions;

  Thread &thread = state.thread();

  if (!isa<PHINode>(thread.prevPc->inst) || !isa<PHINode>(thread.pc->inst)) {
    if (thread.prevPc->inst->getFunction() == thread.pc->inst->getFunction()) {
      thread.liveSet = &thread.prevPc->info->getLiveLocals();
    }
  }

  ++stats::instructions;
  ++state.steppedInstructions;
  thread.prevPc = thread.pc;
  ++thread.pc;

  if (stats::instructions == MaxInstructions)
    haltExecution = true;
}

static inline const llvm::fltSemantics *fpWidthToSemantics(unsigned width) {
  switch (width) {
#if LLVM_VERSION_CODE >= LLVM_VERSION(4, 0)
  case Expr::Int32:
    return &llvm::APFloat::IEEEsingle();
  case Expr::Int64:
    return &llvm::APFloat::IEEEdouble();
  case Expr::Fl80:
    return &llvm::APFloat::x87DoubleExtended();
#else
  case Expr::Int32:
    return &llvm::APFloat::IEEEsingle;
  case Expr::Int64:
    return &llvm::APFloat::IEEEdouble;
  case Expr::Fl80:
    return &llvm::APFloat::x87DoubleExtended;
#endif
  default:
    return 0;
  }
}

void Executor::executeCall(ExecutionState &state, 
                           KInstruction *ki,
                           Function *f,
                           std::vector< ref<Expr> > &arguments) {
  if (PruneStates) {
    state.memoryState.registerFunctionCall(f, arguments);
  }

  if (DebugPrintCalls) {
    auto sid = state.id;
    auto tid = state.tid().to_string();

    std::stringstream tmp;
    tmp << "[state: " << std::setw(6) << sid
        << " thread: " << std::setw(5) << tid
        << "] " << std::setfill(' ') << std::setw(state.stack().size() * 2) << "+";
    if (f->hasName()) {
      llvm::errs() << tmp.str() << f->getName() << '(';
    } else {
      llvm::errs() << tmp.str() << "<unnamed function>(";
    }

    bool first = true;

    for (std::size_t i = 0; i < arguments.size(); i++) {
      if (first) {
        first = false;
      } else {
        llvm::errs() << ", ";
      }

      const auto& argValue = arguments[i];

      if (i < f->arg_size()) {
        const auto* fArg = (f->args().begin() + i);

        if (fArg->hasName()) {
          llvm::errs() << fArg->getName() << " = ";
        }

        if (auto v = dyn_cast<ConstantExpr>(argValue.get())) {
          if (fArg->getType()->isPointerTy()) {
            llvm::errs() << "0x" << v->getAPValue().toString(16, false);
          } else {
            llvm::errs() << v->getAPValue();
          }
        } else {
          llvm::errs() << " <sym>";
        }
      } else {
        if (auto v = dyn_cast<ConstantExpr>(argValue.get())) {
          llvm::errs() << v->getAPValue();
        } else {
          llvm::errs() << " <sym>";
        }
      }
    }

    llvm::errs() << ")\n";
  }

  Instruction *i = ki->inst;
  if (i && isa<DbgInfoIntrinsic>(i))
    return;
  if (f && f->isDeclaration()) {
    switch(f->getIntrinsicID()) {
    case Intrinsic::not_intrinsic:
      // state may be destroyed by this call, cannot touch
      callExternalFunction(state, ki, f, arguments);
      break;
    case Intrinsic::fabs: {
      ref<ConstantExpr> arg =
          toConstant(state, arguments[0], "floating point");
      if (!fpWidthToSemantics(arg->getWidth()))
        return terminateStateOnExecError(
            state, "Unsupported intrinsic llvm.fabs call");

      llvm::APFloat Res(*fpWidthToSemantics(arg->getWidth()),
                        arg->getAPValue());
      Res = llvm::abs(Res);

      bindLocal(ki, state, ConstantExpr::alloc(Res.bitcastToAPInt()));
      break;
    }
    // va_arg is handled by caller and intrinsic lowering, see comment for
    // ExecutionState::varargs
    case Intrinsic::vastart:  {
      const StackFrame &sf = state.stackFrame();

      // varargs can be zero if no varargs were provided
      if (!sf.varargs)
        return;

      // FIXME: This is really specific to the architecture, not the pointer
      // size. This happens to work for x86-32 and x86-64, however.
      Expr::Width WordSize = Context::get().getPointerWidth();
      if (WordSize == Expr::Int32) {
        executeMemoryOperation(state, true, arguments[0], 
                               sf.varargs->getBaseExpr(), 0);
      } else {
        assert(WordSize == Expr::Int64 && "Unknown word size!");

        // x86-64 has quite complicated calling convention. However,
        // instead of implementing it, we can do a simple hack: just
        // make a function believe that all varargs are on stack.
        executeMemoryOperation(state, true, arguments[0],
                               ConstantExpr::create(48, 32), 0); // gp_offset
        executeMemoryOperation(state, true,
                               AddExpr::create(arguments[0], 
                                               ConstantExpr::create(4, 64)),
                               ConstantExpr::create(304, 32), 0); // fp_offset
        executeMemoryOperation(state, true,
                               AddExpr::create(arguments[0], 
                                               ConstantExpr::create(8, 64)),
                               sf.varargs->getBaseExpr(), 0); // overflow_arg_area
        executeMemoryOperation(state, true,
                               AddExpr::create(arguments[0], 
                                               ConstantExpr::create(16, 64)),
                               ConstantExpr::create(0, 64), 0); // reg_save_area
      }
      break;
    }
    case Intrinsic::vaend:
      // va_end is a noop for the interpreter.
      //
      // FIXME: We should validate that the target didn't do something bad
      // with va_end, however (like call it twice).
      break;
        
    case Intrinsic::vacopy:
      // va_copy should have been lowered.
      //
      // FIXME: It would be nice to check for errors in the usage of this as
      // well.
    default:
      klee_error("unknown intrinsic: %s", f->getName().data());
    }

    if (InvokeInst *ii = dyn_cast<InvokeInst>(i)) {
      transferToBasicBlock(ii->getNormalDest(), i->getParent(), state);
    }
  } else {
    // Check if maximum stack size was reached.
    // We currently only count the number of stack frames
    if (RuntimeMaxStackFrames && state.stack().size() > RuntimeMaxStackFrames) {
      terminateStateEarly(state, "Maximum stack size reached.");
      klee_warning("Maximum stack size reached.");
      return;
    }

    // FIXME: I'm not really happy about this reliance on prevPC but it is ok, I
    // guess. This just done to avoid having to pass KInstIterator everywhere
    // instead of the actual instruction, since we can't make a KInstIterator
    // from just an instruction (unlike LLVM).
    KFunction *kf = kmodule->functionMap[f];
    Thread &thread = state.thread();
    state.pushFrame(state.prevPc(), kf);
    thread.pc = kf->instructions;
    thread.liveSet = kf->getLiveLocals(&kf->function->front());
    if (PruneStates) {
      state.memoryState.registerPushFrame(state.tid(), state.stackFrameIndex(),
                                          kf, state.prevPc());
    }

    if (statsTracker) {
      StackFrame* current = &thread.stack.back();
      statsTracker->framePushed(current, &state.stack().at(state.stackFrameIndex() - 1));
    }

     // TODO: support "byval" parameter attribute
     // TODO: support zeroext, signext, sret attributes

    unsigned callingArgs = arguments.size();
    unsigned funcArgs = f->arg_size();
    if (!f->isVarArg()) {
      if (callingArgs > funcArgs) {
        klee_warning_once(f, "calling %s with extra arguments.", 
                          f->getName().data());
      } else if (callingArgs < funcArgs) {
        terminateStateOnError(state, "calling function with too few arguments",
                              User);
        return;
      }
    } else {
      Expr::Width WordSize = Context::get().getPointerWidth();

      if (callingArgs < funcArgs) {
        terminateStateOnError(state, "calling function with too few arguments",
                              User);
        return;
      }

      StackFrame &sf = thread.stack.back();
      unsigned size = 0;
      bool requires16ByteAlignment = false;
      for (unsigned i = funcArgs; i < callingArgs; i++) {
        // FIXME: This is really specific to the architecture, not the pointer
        // size. This happens to work for x86-32 and x86-64, however.
        if (WordSize == Expr::Int32) {
          size += Expr::getMinBytesForWidth(arguments[i]->getWidth());
        } else {
          Expr::Width argWidth = arguments[i]->getWidth();
          // AMD64-ABI 3.5.7p5: Step 7. Align l->overflow_arg_area upwards to a
          // 16 byte boundary if alignment needed by type exceeds 8 byte
          // boundary.
          //
          // Alignment requirements for scalar types is the same as their size
          if (argWidth > Expr::Int64) {
#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 9)
             size = llvm::alignTo(size, 16);
#else
             size = llvm::RoundUpToAlignment(size, 16);
#endif
             requires16ByteAlignment = true;
          }
#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 9)
          size += llvm::alignTo(argWidth, WordSize) / 8;
#else
          size += llvm::RoundUpToAlignment(argWidth, WordSize) / 8;
#endif
        }
      }

      MemoryObject *mo = sf.varargs =
          memory->allocate(size, true, state.prevPc()->inst,
                           thread,
                           state.stackFrameIndex(),
                           (requires16ByteAlignment ? 16 : 8));
      if (!mo && size) {
        terminateStateOnExecError(state, "out of memory (varargs)");
        return;
      }

      if (mo) {
        processMemoryAccess(state, mo, nullptr, 0, MemoryOperation::Type::ALLOC);

        if ((WordSize == Expr::Int64) && (mo->address & 15) &&
            requires16ByteAlignment) {
          // Both 64bit Linux/Glibc and 64bit MacOSX should align to 16 bytes.
          klee_warning_once(
              0, "While allocating varargs: malloc did not align to 16 bytes.");
        }
        ObjectState *os = bindObjectInState(state, mo, true);
        unsigned offset = 0;
        for (unsigned i = funcArgs; i < callingArgs; i++) {
          // FIXME: This is really specific to the architecture, not the pointer
          // size. This happens to work for x86-32 and x86-64, however.
          if (WordSize == Expr::Int32) {
            os->write(offset, arguments[i]);
            offset += Expr::getMinBytesForWidth(arguments[i]->getWidth());
          } else {
            assert(WordSize == Expr::Int64 && "Unknown word size!");

            Expr::Width argWidth = arguments[i]->getWidth();
            if (argWidth > Expr::Int64) {
#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 9)
              offset = llvm::alignTo(offset, 16);
#else
              offset = llvm::RoundUpToAlignment(offset, 16);
#endif
            }
            os->write(offset, arguments[i]);
#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 9)
            offset += llvm::alignTo(argWidth, WordSize) / 8;
#else
            offset += llvm::RoundUpToAlignment(argWidth, WordSize) / 8;
#endif
          }
        }
        if (PruneStates) {
          state.memoryState.registerWrite(*mo, *os);
        }
      }
    }

    unsigned numFormals = f->arg_size();
    for (unsigned i=0; i<numFormals; ++i) 
      bindArgument(kf, i, state, arguments[i]);
  }
}

void Executor::transferToBasicBlock(BasicBlock *dst, BasicBlock *src, 
                                    ExecutionState &state) {
  // Note that in general phi nodes can reuse phi values from the same
  // block but the incoming value is the eval() result *before* the
  // execution of any phi nodes. this is pathological and doesn't
  // really seem to occur, but just in case we run the PhiCleanerPass
  // which makes sure this cannot happen and so it is safe to just
  // eval things in order. The PhiCleanerPass also makes sure that all
  // incoming blocks have the same order for each PHINode so we only
  // have to compute the index once.
  //
  // With that done we simply set an index in the state so that PHI
  // instructions know which argument to eval, set the pc, and continue.

  Thread &thread = state.thread();

  // XXX this lookup has to go ?
  KFunction *kf = thread.stack.back().kf;
  unsigned entry = kf->basicBlockEntry[dst];
  thread.pc = &kf->instructions[entry];
  if (thread.pc->inst->getOpcode() == Instruction::PHI) {
    PHINode *first = static_cast<PHINode*>(thread.pc->inst);
    thread.incomingBBIndex = first->getBasicBlockIndex(src);
    thread.liveSet = &thread.prevPc->info->getLiveLocals();
  } else {
    thread.liveSet = kf->getLiveLocals(dst);
  }
}

/// Compute the true target of a function call, resolving LLVM aliases
/// and bitcasts.
Function* Executor::getTargetFunction(Value *calledVal, ExecutionState &state) {
  SmallPtrSet<const GlobalValue*, 3> Visited;

  Constant *c = dyn_cast<Constant>(calledVal);
  if (!c)
    return 0;

  while (true) {
    if (GlobalValue *gv = dyn_cast<GlobalValue>(c)) {
      if (!Visited.insert(gv).second)
        return 0;

      if (Function *f = dyn_cast<Function>(gv))
        return f;
      else if (GlobalAlias *ga = dyn_cast<GlobalAlias>(gv))
        c = ga->getAliasee();
      else
        return 0;
    } else if (llvm::ConstantExpr *ce = dyn_cast<llvm::ConstantExpr>(c)) {
      if (ce->getOpcode()==Instruction::BitCast)
        c = ce->getOperand(0);
      else
        return 0;
    } else
      return 0;
  }
}

void Executor::executeInstruction(ExecutionState &state, KInstruction *ki) {
  Instruction *i = ki->inst;
  Thread &thread = state.thread();

  assert(state.threadState() == ThreadState::Runnable);

  switch (i->getOpcode()) {
    // Control flow
  case Instruction::Ret: {
    ReturnInst *ri = cast<ReturnInst>(i);
    const StackFrame &sf = state.stackFrame();
    KInstIterator kcaller = sf.caller;
    Instruction *caller = kcaller ? kcaller->inst : 0;
    bool isVoidReturn = (ri->getNumOperands() == 0);
    ref<Expr> result = ConstantExpr::alloc(0, Expr::Bool);

    if (PruneStates) {
      Function *callee = sf.kf->function;
      state.memoryState.registerFunctionRet(callee);
    }

    if (!isVoidReturn) {
      result = eval(ki, 0, state).value;
    }

    if (DebugPrintCalls) {
      auto sid = state.id;
      auto tid = state.tid().to_string();
      auto* f = sf.kf->function;

      std::stringstream tmp;
      tmp << "[state: " << std::setw(6) << sid
          << " thread: " << std::setw(5) << tid
          << "] " << std::setfill(' ') << std::setw(state.stackFrameIndex() * 2) << "-";
      if (f->hasName()) {
        llvm::errs() << tmp.str() << f->getName() << " -> ";
      } else {
        llvm::errs() << tmp.str() << "<unnamed function> -> ";
      }

      if (isVoidReturn) {
        llvm::errs() << " <void>";
      } else {
        if (auto v = dyn_cast<ConstantExpr>(result.get())) {
          if (caller && caller->getType()->isPointerTy()) {
            llvm::errs() << "0x" << v->getAPValue().toString(16, false);
          } else {
            llvm::errs() << v->getAPValue();
          }
        } else {
          llvm::errs() << " <sym>";
        }
      }

      llvm::errs() << '\n';
    }
    
    if (state.stackFrameIndex() == 0) {
      assert(!caller && "caller set on initial stack frame");
      // only happens without uClibC or POSIX runtime;
      // hence exit() is called implicitly return from main
      assert(kmodule->module->getFunction("__klee_posix_wrapped_main") == nullptr
             && kmodule->module->getFunction("__uClibc_main") == nullptr);
      exitCurrentThread(state, true);
      porEventManager.registerThreadExit(state, state.tid(), false);
    } else {
      // When we pop the stack frame, we free the memory regions
      // this means that we need to check these memory accesses
      for (auto mo : state.stackFrame().allocas) {
        processMemoryAccess(state, mo, nullptr, 0, MemoryOperation::Type::FREE);
      }

      state.popFrameOfThread();

      if (statsTracker)
        statsTracker->framePopped(state);

      if (InvokeInst *ii = dyn_cast<InvokeInst>(caller)) {
        transferToBasicBlock(ii->getNormalDest(), caller->getParent(), state);
      } else {
        thread.pc = kcaller;
        ++thread.pc;
        thread.liveSet = &kcaller->info->getLiveLocals();
      }

      if (!isVoidReturn) {
        Type *t = caller->getType();
        if (t != Type::getVoidTy(i->getContext())) {
          // may need to do coercion due to bitcasts
          Expr::Width from = result->getWidth();
          Expr::Width to = getWidthForLLVMType(t);
            
          if (from != to) {
            CallSite cs = (isa<InvokeInst>(caller) ? CallSite(cast<InvokeInst>(caller)) : 
                           CallSite(cast<CallInst>(caller)));

            // XXX need to check other param attrs ?
#if LLVM_VERSION_CODE >= LLVM_VERSION(5, 0)
            bool isSExt = cs.hasRetAttr(llvm::Attribute::SExt);
#else
            bool isSExt = cs.paramHasAttr(0, llvm::Attribute::SExt);
#endif
            if (isSExt) {
              result = SExtExpr::create(result, to);
            } else {
              result = ZExtExpr::create(result, to);
            }
          }

          bindLocal(kcaller, state, result);
        }
      } else {
        // We check that the return value has no users instead of
        // checking the type, since C defaults to returning int for
        // undeclared functions.
        if (!caller->use_empty()) {
          terminateStateOnExecError(state, "return void when caller expected a result");
        }
      }
    }      
    break;
  }
  case Instruction::Br: {
    BranchInst *bi = cast<BranchInst>(i);
    if (bi->isUnconditional()) {
      transferToBasicBlock(bi->getSuccessor(0), bi->getParent(), state);
    } else {
      // FIXME: Find a way that we don't have this hidden dependency.
      assert(bi->getCondition() == bi->getOperand(0) &&
             "Wrong operand index!");
      ref<Expr> cond = eval(ki, 0, state).value;

      cond = optimizer.optimizeExpr(cond, false);
      Executor::StatePair branches = fork(state, cond, false);

      // NOTE: There is a hidden dependency here, markBranchVisited
      // requires that we still be in the context of the branch
      // instruction (it reuses its statistic id). Should be cleaned
      // up with convenient instruction specific data.
      if (statsTracker && state.stackFrame().kf->trackCoverage)
        statsTracker->markBranchVisited(branches.first, branches.second);

      if (branches.first)
        transferToBasicBlock(bi->getSuccessor(0), bi->getParent(), *branches.first);
      if (branches.second)
        transferToBasicBlock(bi->getSuccessor(1), bi->getParent(), *branches.second);
    }
    break;
  }
  case Instruction::IndirectBr: {
    // implements indirect branch to a label within the current function
    const auto bi = cast<IndirectBrInst>(i);
    auto address = eval(ki, 0, state).value;
    // FIXME: address = toUnique(state, address);

    // concrete address
    if (const auto CE = dyn_cast<ConstantExpr>(address.get())) {
      const auto bb_address = (BasicBlock *) CE->getZExtValue(Context::get().getPointerWidth());
      transferToBasicBlock(bb_address, bi->getParent(), state);
      break;
    }

    // symbolic address
    const auto numDestinations = bi->getNumDestinations();
    std::vector<BasicBlock *> targets;
    targets.reserve(numDestinations);
    std::vector<std::pair<std::size_t, ref<Expr>>> expressions;
    expressions.reserve(numDestinations);

    ref<Expr> errorCase = ConstantExpr::alloc(1, Expr::Bool);
    SmallPtrSet<BasicBlock *, 5> destinations;
    // collect and check destinations from label list
    for (unsigned k = 0; k < numDestinations; ++k) {
      // filter duplicates
      const auto d = bi->getDestination(k);
      if (destinations.count(d)) continue;
      destinations.insert(d);

      // create address expression
      const auto PE = Expr::createPointer(reinterpret_cast<std::uint64_t>(d));
      ref<Expr> e = EqExpr::create(address, PE);

      // exclude address from errorCase
      errorCase = AndExpr::create(errorCase, Expr::createIsZero(e));

      // check feasibility
      bool result;
      bool success __attribute__ ((unused)) = solver->mayBeTrue(state, e, result);
      assert(success && "FIXME: Unhandled solver failure");
      if (result) {
        targets.push_back(d);
        expressions.emplace_back(k, e);
      }
    }
    // check errorCase feasibility
    bool result;
    bool success __attribute__ ((unused)) = solver->mayBeTrue(state, errorCase, result);
    assert(success && "FIXME: Unhandled solver failure");
    if (result) {
      assert(expressions.size() <= numDestinations);
      expressions.emplace_back(numDestinations, errorCase);
    }

    // fork states
    std::vector<ExecutionState *> branches;
    branch(state, expressions, branches);

    // terminate error state
    if (result) {
      terminateStateOnExecError(*branches.back(), "indirectbr: illegal label address");
      branches.pop_back();
    }

    // branch states to resp. target blocks
    assert(targets.size() == branches.size());
    for (std::vector<ExecutionState *>::size_type k = 0; k < branches.size(); ++k) {
      if (branches[k]) {
        transferToBasicBlock(targets[k], bi->getParent(), *branches[k]);
      }
    }

    break;
  }
  case Instruction::Switch: {
    SwitchInst *si = cast<SwitchInst>(i);
    ref<Expr> cond = eval(ki, 0, state).value;

    // FIXME: cond = toUnique(state, cond);
    if (ConstantExpr *CE = dyn_cast<ConstantExpr>(cond)) {
      // Somewhat gross to create these all the time, but fine till we
      // switch to an internal rep.
      llvm::IntegerType *Ty = cast<IntegerType>(si->getCondition()->getType());
      ConstantInt *ci = ConstantInt::get(Ty, CE->getZExtValue());
#if LLVM_VERSION_CODE >= LLVM_VERSION(5, 0)
      unsigned index = si->findCaseValue(ci)->getSuccessorIndex();
#else
      unsigned index = si->findCaseValue(ci).getSuccessorIndex();
#endif
      transferToBasicBlock(si->getSuccessor(index), si->getParent(), state);
    } else {
      // Handle possible different branch targets

      // We have the following assumptions:
      // - each case value is mutual exclusive to all other values
      // - order of case branches is based on the order of the expressions of
      //   the case values, still default is handled last
      std::vector<std::pair<std::size_t, BasicBlock *>> bbOrder;
      std::map<BasicBlock *, ref<Expr> > branchTargets;

      std::vector<std::pair<ref<Expr>, BasicBlock *>> expressionOrder;

      // Iterate through all non-default cases and order them by expressions
      for (auto i : si->cases()) {
        ref<Expr> value = evalConstant(i.getCaseValue(), state.tid());

        BasicBlock *caseSuccessor = i.getCaseSuccessor();
        expressionOrder.emplace_back(value, caseSuccessor);
      }

      // Track default branch values
      ref<Expr> defaultValue = ConstantExpr::alloc(1, Expr::Bool);

      // iterate through all non-default cases but in order of the expressions
      for (std::size_t i = 0; i < expressionOrder.size(); ++i) {
        auto it = expressionOrder[i];
        ref<Expr> match = EqExpr::create(cond, it.first);

        // skip if case has same successor basic block as default case
        // (should work even with phi nodes as a switch is a single terminating instruction)
        if (it.second == si->getDefaultDest()) continue;

        // Make sure that the default value does not contain this target's value
        defaultValue = AndExpr::create(defaultValue, Expr::createIsZero(match));

        // Check if control flow could take this case
        bool result;
        match = optimizer.optimizeExpr(match, false);
        bool success = solver->mayBeTrue(state, match, result);
        assert(success && "FIXME: Unhandled solver failure");
        (void) success;
        if (result) {
          BasicBlock *caseSuccessor = it.second;

          // Handle the case that a basic block might be the target of multiple
          // switch cases.
          // Currently we generate an expression containing all switch-case
          // values for the same target basic block. We spare us forking too
          // many times but we generate more complex condition expressions
          // TODO Add option to allow to choose between those behaviors
          std::pair<std::map<BasicBlock *, ref<Expr> >::iterator, bool> res =
              branchTargets.insert(std::make_pair(
                  caseSuccessor, ConstantExpr::alloc(0, Expr::Bool)));

          res.first->second = OrExpr::create(match, res.first->second);

          // Only add basic blocks which have not been target of a branch yet
          if (res.second) {
            bbOrder.emplace_back(i, caseSuccessor);
          }
        }
      }

      // Check if control could take the default case
      defaultValue = optimizer.optimizeExpr(defaultValue, false);
      bool res;
      bool success = solver->mayBeTrue(state, defaultValue, res);
      assert(success && "FIXME: Unhandled solver failure");
      (void) success;
      if (res) {
        std::pair<std::map<BasicBlock *, ref<Expr> >::iterator, bool> ret =
            branchTargets.insert(
                std::make_pair(si->getDefaultDest(), defaultValue));
        if (ret.second) {
          bbOrder.emplace_back(expressionOrder.size(), si->getDefaultDest());
        }
      }

      // Fork the current state with each state having one of the possible
      // successors of this switch
      std::vector<std::pair<std::size_t, ref<Expr>>> conditions;
      for (auto &[choice, bb] : bbOrder) {
        conditions.emplace_back(choice, branchTargets[bb]);
      }
      std::vector<ExecutionState*> branches;
      branch(state, conditions, branches);

      std::vector<ExecutionState*>::iterator bit = branches.begin();
      for (auto &[_, bb] : bbOrder) {
        ExecutionState *es = *bit;
        if (es)
          transferToBasicBlock(bb, si->getParent(), *es);
        ++bit;
      }
    }
    break;
  }
  case Instruction::Unreachable:
    // Note that this is not necessarily an internal bug, llvm will
    // generate unreachable instructions in cases where it knows the
    // program will crash. So it is effectively a SEGV or internal
    // error.
    terminateStateOnExecError(state, "reached \"unreachable\" instruction");
    break;

  case Instruction::Invoke:
  case Instruction::Call: {
    // Ignore debug intrinsic calls
    if (isa<DbgInfoIntrinsic>(i))
      break;
    CallSite cs(i);

    unsigned numArgs = cs.arg_size();
    Value *fp = cs.getCalledValue();
    Function *f = getTargetFunction(fp, state);

    if (isa<InlineAsm>(fp)) {
      terminateStateOnExecError(state, "inline assembly is unsupported");
      break;
    }
    // evaluate arguments
    std::vector< ref<Expr> > arguments;
    arguments.reserve(numArgs);

    for (unsigned j=0; j<numArgs; ++j)
      arguments.push_back(eval(ki, j+1, state).value);

    if (f) {
      const FunctionType *fType = 
        dyn_cast<FunctionType>(cast<PointerType>(f->getType())->getElementType());
      const FunctionType *fpType =
        dyn_cast<FunctionType>(cast<PointerType>(fp->getType())->getElementType());

      // special case the call with a bitcast case
      if (fType != fpType) {
        assert(fType && fpType && "unable to get function type");

        // XXX check result coercion

        // XXX this really needs thought and validation
        unsigned i=0;
        for (std::vector< ref<Expr> >::iterator
               ai = arguments.begin(), ie = arguments.end();
             ai != ie; ++ai) {
          Expr::Width to, from = (*ai)->getWidth();
            
          if (i<fType->getNumParams()) {
            to = getWidthForLLVMType(fType->getParamType(i));

            if (from != to) {
              // XXX need to check other param attrs ?
#if LLVM_VERSION_CODE >= LLVM_VERSION(5, 0)
              bool isSExt = cs.paramHasAttr(i, llvm::Attribute::SExt);
#else
              bool isSExt = cs.paramHasAttr(i+1, llvm::Attribute::SExt);
#endif
              if (isSExt) {
                arguments[i] = SExtExpr::create(arguments[i], to);
              } else {
                arguments[i] = ZExtExpr::create(arguments[i], to);
              }
            }
          }
            
          i++;
        }
      }

      executeCall(state, ki, f, arguments);
    } else {
      ref<Expr> v = eval(ki, 0, state).value;

      ExecutionState *free = &state;
      bool hasInvalid = false, first = true;

      /* XXX This is wasteful, no need to do a full evaluate since we
         have already got a value. But in the end the caches should
         handle it for us, albeit with some overhead. */
      do {
        v = optimizer.optimizeExpr(v, true);
        ref<ConstantExpr> value;
        bool success = solver->getValue(*free, v, value);
        assert(success && "FIXME: Unhandled solver failure");
        (void) success;
        StatePair res = fork(*free, EqExpr::create(v, value), true);
        if (res.first) {
          uint64_t addr = value->getZExtValue();
          if (legalFunctions.count(addr)) {
            f = (Function*) addr;

            // Don't give warning on unique resolution
            if (res.second || !first)
              klee_warning_once(reinterpret_cast<void*>(addr),
                                "resolved symbolic function pointer to: %s",
                                f->getName().data());

            executeCall(*res.first, ki, f, arguments);
          } else {
            if (!hasInvalid) {
              terminateStateOnExecError(state, "invalid function pointer");
              hasInvalid = true;
            }
          }
        }

        first = false;
        free = res.second;
      } while (free);
    }
    break;
  }
  case Instruction::PHI: {
    ref<Expr> result = eval(ki, thread.incomingBBIndex, state).value;
    bindLocal(ki, state, result);
    assert(ki == state.prevPc() && "executing instruction different from state.prevPc");
    break;
  }

    // Special instructions
  case Instruction::Select: {
    // NOTE: It is not required that operands 1 and 2 be of scalar type.
    ref<Expr> cond = eval(ki, 0, state).value;
    ref<Expr> tExpr = eval(ki, 1, state).value;
    ref<Expr> fExpr = eval(ki, 2, state).value;
    ref<Expr> result = SelectExpr::create(cond, tExpr, fExpr);
    bindLocal(ki, state, result);
    break;
  }

  case Instruction::VAArg:
    terminateStateOnExecError(state, "unexpected VAArg instruction");
    break;

    // Arithmetic / logical

  case Instruction::Add: {
    ref<Expr> left = eval(ki, 0, state).value;
    ref<Expr> right = eval(ki, 1, state).value;
    bindLocal(ki, state, AddExpr::create(left, right));
    break;
  }

  case Instruction::Sub: {
    ref<Expr> left = eval(ki, 0, state).value;
    ref<Expr> right = eval(ki, 1, state).value;
    bindLocal(ki, state, SubExpr::create(left, right));
    break;
  }
 
  case Instruction::Mul: {
    ref<Expr> left = eval(ki, 0, state).value;
    ref<Expr> right = eval(ki, 1, state).value;
    bindLocal(ki, state, MulExpr::create(left, right));
    break;
  }

  case Instruction::UDiv: {
    ref<Expr> left = eval(ki, 0, state).value;
    ref<Expr> right = eval(ki, 1, state).value;
    ref<Expr> result = UDivExpr::create(left, right);
    bindLocal(ki, state, result);
    break;
  }

  case Instruction::SDiv: {
    ref<Expr> left = eval(ki, 0, state).value;
    ref<Expr> right = eval(ki, 1, state).value;
    ref<Expr> result = SDivExpr::create(left, right);
    bindLocal(ki, state, result);
    break;
  }

  case Instruction::URem: {
    ref<Expr> left = eval(ki, 0, state).value;
    ref<Expr> right = eval(ki, 1, state).value;
    ref<Expr> result = URemExpr::create(left, right);
    bindLocal(ki, state, result);
    break;
  }

  case Instruction::SRem: {
    ref<Expr> left = eval(ki, 0, state).value;
    ref<Expr> right = eval(ki, 1, state).value;
    ref<Expr> result = SRemExpr::create(left, right);
    bindLocal(ki, state, result);
    break;
  }

  case Instruction::And: {
    ref<Expr> left = eval(ki, 0, state).value;
    ref<Expr> right = eval(ki, 1, state).value;
    ref<Expr> result = AndExpr::create(left, right);
    bindLocal(ki, state, result);
    break;
  }

  case Instruction::Or: {
    ref<Expr> left = eval(ki, 0, state).value;
    ref<Expr> right = eval(ki, 1, state).value;
    ref<Expr> result = OrExpr::create(left, right);
    bindLocal(ki, state, result);
    break;
  }

  case Instruction::Xor: {
    ref<Expr> left = eval(ki, 0, state).value;
    ref<Expr> right = eval(ki, 1, state).value;
    ref<Expr> result = XorExpr::create(left, right);
    bindLocal(ki, state, result);
    break;
  }

  case Instruction::Shl: {
    ref<Expr> left = eval(ki, 0, state).value;
    ref<Expr> right = eval(ki, 1, state).value;
    ref<Expr> result = ShlExpr::create(left, right);
    bindLocal(ki, state, result);
    break;
  }

  case Instruction::LShr: {
    ref<Expr> left = eval(ki, 0, state).value;
    ref<Expr> right = eval(ki, 1, state).value;
    ref<Expr> result = LShrExpr::create(left, right);
    bindLocal(ki, state, result);
    break;
  }

  case Instruction::AShr: {
    ref<Expr> left = eval(ki, 0, state).value;
    ref<Expr> right = eval(ki, 1, state).value;
    ref<Expr> result = AShrExpr::create(left, right);
    bindLocal(ki, state, result);
    break;
  }

    // Compare

  case Instruction::ICmp: {
    CmpInst *ci = cast<CmpInst>(i);
    ICmpInst *ii = cast<ICmpInst>(ci);

    switch(ii->getPredicate()) {
    case ICmpInst::ICMP_EQ: {
      ref<Expr> left = eval(ki, 0, state).value;
      ref<Expr> right = eval(ki, 1, state).value;
      ref<Expr> result = EqExpr::create(left, right);
      bindLocal(ki, state, result);
      break;
    }

    case ICmpInst::ICMP_NE: {
      ref<Expr> left = eval(ki, 0, state).value;
      ref<Expr> right = eval(ki, 1, state).value;
      ref<Expr> result = NeExpr::create(left, right);
      bindLocal(ki, state, result);
      break;
    }

    case ICmpInst::ICMP_UGT: {
      ref<Expr> left = eval(ki, 0, state).value;
      ref<Expr> right = eval(ki, 1, state).value;
      ref<Expr> result = UgtExpr::create(left, right);
      bindLocal(ki, state,result);
      break;
    }

    case ICmpInst::ICMP_UGE: {
      ref<Expr> left = eval(ki, 0, state).value;
      ref<Expr> right = eval(ki, 1, state).value;
      ref<Expr> result = UgeExpr::create(left, right);
      bindLocal(ki, state, result);
      break;
    }

    case ICmpInst::ICMP_ULT: {
      ref<Expr> left = eval(ki, 0, state).value;
      ref<Expr> right = eval(ki, 1, state).value;
      ref<Expr> result = UltExpr::create(left, right);
      bindLocal(ki, state, result);
      break;
    }

    case ICmpInst::ICMP_ULE: {
      ref<Expr> left = eval(ki, 0, state).value;
      ref<Expr> right = eval(ki, 1, state).value;
      ref<Expr> result = UleExpr::create(left, right);
      bindLocal(ki, state, result);
      break;
    }

    case ICmpInst::ICMP_SGT: {
      ref<Expr> left = eval(ki, 0, state).value;
      ref<Expr> right = eval(ki, 1, state).value;
      ref<Expr> result = SgtExpr::create(left, right);
      bindLocal(ki, state, result);
      break;
    }

    case ICmpInst::ICMP_SGE: {
      ref<Expr> left = eval(ki, 0, state).value;
      ref<Expr> right = eval(ki, 1, state).value;
      ref<Expr> result = SgeExpr::create(left, right);
      bindLocal(ki, state, result);
      break;
    }

    case ICmpInst::ICMP_SLT: {
      ref<Expr> left = eval(ki, 0, state).value;
      ref<Expr> right = eval(ki, 1, state).value;
      ref<Expr> result = SltExpr::create(left, right);
      bindLocal(ki, state, result);
      break;
    }

    case ICmpInst::ICMP_SLE: {
      ref<Expr> left = eval(ki, 0, state).value;
      ref<Expr> right = eval(ki, 1, state).value;
      ref<Expr> result = SleExpr::create(left, right);
      bindLocal(ki, state, result);
      break;
    }

    default:
      terminateStateOnExecError(state, "invalid ICmp predicate");
    }
    break;
  }
 
    // Memory instructions...
  case Instruction::Alloca: {
    AllocaInst *ai = cast<AllocaInst>(i);
    unsigned elementSize = 
      kmodule->targetData->getTypeStoreSize(ai->getAllocatedType());
    ref<Expr> size = Expr::createPointer(elementSize);
    if (ai->isArrayAllocation()) {
      ref<Expr> count = eval(ki, 0, state).value;
      count = Expr::createZExtToPointerWidth(count);
      size = MulExpr::create(size, count);
    }
    executeAlloc(state, size, true, ki);
    break;
  }

  case Instruction::Load: {
    ref<Expr> base = eval(ki, 0, state).value;
    executeMemoryOperation(state, false, base, 0, ki);
    break;
  }
  case Instruction::Store: {
    ref<Expr> base = eval(ki, 1, state).value;
    ref<Expr> value = eval(ki, 0, state).value;
    executeMemoryOperation(state, true, base, value, 0);
    break;
  }

  case Instruction::GetElementPtr: {
    KGEPInstruction *kgepi = static_cast<KGEPInstruction*>(ki);
    ref<Expr> base = eval(ki, 0, state).value;

    for (std::vector< std::pair<unsigned, uint64_t> >::iterator 
           it = kgepi->indices.begin(), ie = kgepi->indices.end(); 
         it != ie; ++it) {
      uint64_t elementSize = it->second;
      ref<Expr> index = eval(ki, it->first, state).value;
      base = AddExpr::create(base,
                             MulExpr::create(Expr::createSExtToPointerWidth(index),
                                             Expr::createPointer(elementSize)));
    }
    if (kgepi->offset)
      base = AddExpr::create(base,
                             Expr::createPointer(kgepi->offset));
    bindLocal(ki, state, base);
    break;
  }

    // Conversion
  case Instruction::Trunc: {
    CastInst *ci = cast<CastInst>(i);
    ref<Expr> result = ExtractExpr::create(eval(ki, 0, state).value,
                                           0,
                                           getWidthForLLVMType(ci->getType()));
    bindLocal(ki, state, result);
    break;
  }
  case Instruction::ZExt: {
    CastInst *ci = cast<CastInst>(i);
    ref<Expr> result = ZExtExpr::create(eval(ki, 0, state).value,
                                        getWidthForLLVMType(ci->getType()));
    bindLocal(ki, state, result);
    break;
  }
  case Instruction::SExt: {
    CastInst *ci = cast<CastInst>(i);
    ref<Expr> result = SExtExpr::create(eval(ki, 0, state).value,
                                        getWidthForLLVMType(ci->getType()));
    bindLocal(ki, state, result);
    break;
  }

  case Instruction::IntToPtr: {
    CastInst *ci = cast<CastInst>(i);
    Expr::Width pType = getWidthForLLVMType(ci->getType());
    ref<Expr> arg = eval(ki, 0, state).value;
    bindLocal(ki, state, ZExtExpr::create(arg, pType));
    break;
  }
  case Instruction::PtrToInt: {
    CastInst *ci = cast<CastInst>(i);
    Expr::Width iType = getWidthForLLVMType(ci->getType());
    ref<Expr> arg = eval(ki, 0, state).value;
    bindLocal(ki, state, ZExtExpr::create(arg, iType));
    break;
  }

  case Instruction::BitCast: {
    ref<Expr> result = eval(ki, 0, state).value;
    bindLocal(ki, state, result);
    break;
  }

    // Floating point instructions

  case Instruction::FAdd: {
    ref<ConstantExpr> left = toConstant(state, eval(ki, 0, state).value,
                                        "floating point");
    ref<ConstantExpr> right = toConstant(state, eval(ki, 1, state).value,
                                         "floating point");
    if (!fpWidthToSemantics(left->getWidth()) ||
        !fpWidthToSemantics(right->getWidth()))
      return terminateStateOnExecError(state, "Unsupported FAdd operation");

    llvm::APFloat Res(*fpWidthToSemantics(left->getWidth()), left->getAPValue());
    Res.add(APFloat(*fpWidthToSemantics(right->getWidth()),right->getAPValue()), APFloat::rmNearestTiesToEven);
    bindLocal(ki, state, ConstantExpr::alloc(Res.bitcastToAPInt()));
    break;
  }

  case Instruction::FSub: {
    ref<ConstantExpr> left = toConstant(state, eval(ki, 0, state).value,
                                        "floating point");
    ref<ConstantExpr> right = toConstant(state, eval(ki, 1, state).value,
                                         "floating point");
    if (!fpWidthToSemantics(left->getWidth()) ||
        !fpWidthToSemantics(right->getWidth()))
      return terminateStateOnExecError(state, "Unsupported FSub operation");
    llvm::APFloat Res(*fpWidthToSemantics(left->getWidth()), left->getAPValue());
    Res.subtract(APFloat(*fpWidthToSemantics(right->getWidth()), right->getAPValue()), APFloat::rmNearestTiesToEven);
    bindLocal(ki, state, ConstantExpr::alloc(Res.bitcastToAPInt()));
    break;
  }

  case Instruction::FMul: {
    ref<ConstantExpr> left = toConstant(state, eval(ki, 0, state).value,
                                        "floating point");
    ref<ConstantExpr> right = toConstant(state, eval(ki, 1, state).value,
                                         "floating point");
    if (!fpWidthToSemantics(left->getWidth()) ||
        !fpWidthToSemantics(right->getWidth()))
      return terminateStateOnExecError(state, "Unsupported FMul operation");

    llvm::APFloat Res(*fpWidthToSemantics(left->getWidth()), left->getAPValue());
    Res.multiply(APFloat(*fpWidthToSemantics(right->getWidth()), right->getAPValue()), APFloat::rmNearestTiesToEven);
    bindLocal(ki, state, ConstantExpr::alloc(Res.bitcastToAPInt()));
    break;
  }

  case Instruction::FDiv: {
    ref<ConstantExpr> left = toConstant(state, eval(ki, 0, state).value,
                                        "floating point");
    ref<ConstantExpr> right = toConstant(state, eval(ki, 1, state).value,
                                         "floating point");
    if (!fpWidthToSemantics(left->getWidth()) ||
        !fpWidthToSemantics(right->getWidth()))
      return terminateStateOnExecError(state, "Unsupported FDiv operation");

    llvm::APFloat Res(*fpWidthToSemantics(left->getWidth()), left->getAPValue());
    Res.divide(APFloat(*fpWidthToSemantics(right->getWidth()), right->getAPValue()), APFloat::rmNearestTiesToEven);
    bindLocal(ki, state, ConstantExpr::alloc(Res.bitcastToAPInt()));
    break;
  }

  case Instruction::FRem: {
    ref<ConstantExpr> left = toConstant(state, eval(ki, 0, state).value,
                                        "floating point");
    ref<ConstantExpr> right = toConstant(state, eval(ki, 1, state).value,
                                         "floating point");
    if (!fpWidthToSemantics(left->getWidth()) ||
        !fpWidthToSemantics(right->getWidth()))
      return terminateStateOnExecError(state, "Unsupported FRem operation");
    llvm::APFloat Res(*fpWidthToSemantics(left->getWidth()), left->getAPValue());
    Res.mod(
        APFloat(*fpWidthToSemantics(right->getWidth()), right->getAPValue()));
    bindLocal(ki, state, ConstantExpr::alloc(Res.bitcastToAPInt()));
    break;
  }

  case Instruction::FPTrunc: {
    FPTruncInst *fi = cast<FPTruncInst>(i);
    Expr::Width resultType = getWidthForLLVMType(fi->getType());
    ref<ConstantExpr> arg = toConstant(state, eval(ki, 0, state).value,
                                       "floating point");
    if (!fpWidthToSemantics(arg->getWidth()) || resultType > arg->getWidth())
      return terminateStateOnExecError(state, "Unsupported FPTrunc operation");

    llvm::APFloat Res(*fpWidthToSemantics(arg->getWidth()), arg->getAPValue());
    bool losesInfo = false;
    Res.convert(*fpWidthToSemantics(resultType),
                llvm::APFloat::rmNearestTiesToEven,
                &losesInfo);
    bindLocal(ki, state, ConstantExpr::alloc(Res));
    break;
  }

  case Instruction::FPExt: {
    FPExtInst *fi = cast<FPExtInst>(i);
    Expr::Width resultType = getWidthForLLVMType(fi->getType());
    ref<ConstantExpr> arg = toConstant(state, eval(ki, 0, state).value,
                                        "floating point");
    if (!fpWidthToSemantics(arg->getWidth()) || arg->getWidth() > resultType)
      return terminateStateOnExecError(state, "Unsupported FPExt operation");
    llvm::APFloat Res(*fpWidthToSemantics(arg->getWidth()), arg->getAPValue());
    bool losesInfo = false;
    Res.convert(*fpWidthToSemantics(resultType),
                llvm::APFloat::rmNearestTiesToEven,
                &losesInfo);
    bindLocal(ki, state, ConstantExpr::alloc(Res));
    break;
  }

  case Instruction::FPToUI: {
    FPToUIInst *fi = cast<FPToUIInst>(i);
    Expr::Width resultType = getWidthForLLVMType(fi->getType());
    ref<ConstantExpr> arg = toConstant(state, eval(ki, 0, state).value,
                                       "floating point");
    if (!fpWidthToSemantics(arg->getWidth()) || resultType > 64)
      return terminateStateOnExecError(state, "Unsupported FPToUI operation");

    llvm::APFloat Arg(*fpWidthToSemantics(arg->getWidth()), arg->getAPValue());
    uint64_t value = 0;
    bool isExact = true;
#if LLVM_VERSION_CODE >= LLVM_VERSION(5, 0)
    auto valueRef = makeMutableArrayRef(value);
#else
    uint64_t *valueRef = &value;
#endif
    Arg.convertToInteger(valueRef, resultType, false,
                         llvm::APFloat::rmTowardZero, &isExact);
    bindLocal(ki, state, ConstantExpr::alloc(value, resultType));
    break;
  }

  case Instruction::FPToSI: {
    FPToSIInst *fi = cast<FPToSIInst>(i);
    Expr::Width resultType = getWidthForLLVMType(fi->getType());
    ref<ConstantExpr> arg = toConstant(state, eval(ki, 0, state).value,
                                       "floating point");
    if (!fpWidthToSemantics(arg->getWidth()) || resultType > 64)
      return terminateStateOnExecError(state, "Unsupported FPToSI operation");
    llvm::APFloat Arg(*fpWidthToSemantics(arg->getWidth()), arg->getAPValue());

    uint64_t value = 0;
    bool isExact = true;
#if LLVM_VERSION_CODE >= LLVM_VERSION(5, 0)
    auto valueRef = makeMutableArrayRef(value);
#else
    uint64_t *valueRef = &value;
#endif
    Arg.convertToInteger(valueRef, resultType, true,
                         llvm::APFloat::rmTowardZero, &isExact);
    bindLocal(ki, state, ConstantExpr::alloc(value, resultType));
    break;
  }

  case Instruction::UIToFP: {
    UIToFPInst *fi = cast<UIToFPInst>(i);
    Expr::Width resultType = getWidthForLLVMType(fi->getType());
    ref<ConstantExpr> arg = toConstant(state, eval(ki, 0, state).value,
                                       "floating point");
    const llvm::fltSemantics *semantics = fpWidthToSemantics(resultType);
    if (!semantics)
      return terminateStateOnExecError(state, "Unsupported UIToFP operation");
    llvm::APFloat f(*semantics, 0);
    f.convertFromAPInt(arg->getAPValue(), false,
                       llvm::APFloat::rmNearestTiesToEven);

    bindLocal(ki, state, ConstantExpr::alloc(f));
    break;
  }

  case Instruction::SIToFP: {
    SIToFPInst *fi = cast<SIToFPInst>(i);
    Expr::Width resultType = getWidthForLLVMType(fi->getType());
    ref<ConstantExpr> arg = toConstant(state, eval(ki, 0, state).value,
                                       "floating point");
    const llvm::fltSemantics *semantics = fpWidthToSemantics(resultType);
    if (!semantics)
      return terminateStateOnExecError(state, "Unsupported SIToFP operation");
    llvm::APFloat f(*semantics, 0);
    f.convertFromAPInt(arg->getAPValue(), true,
                       llvm::APFloat::rmNearestTiesToEven);

    bindLocal(ki, state, ConstantExpr::alloc(f));
    break;
  }

  case Instruction::FCmp: {
    FCmpInst *fi = cast<FCmpInst>(i);
    ref<ConstantExpr> left = toConstant(state, eval(ki, 0, state).value,
                                        "floating point");
    ref<ConstantExpr> right = toConstant(state, eval(ki, 1, state).value,
                                         "floating point");
    if (!fpWidthToSemantics(left->getWidth()) ||
        !fpWidthToSemantics(right->getWidth()))
      return terminateStateOnExecError(state, "Unsupported FCmp operation");

    APFloat LHS(*fpWidthToSemantics(left->getWidth()),left->getAPValue());
    APFloat RHS(*fpWidthToSemantics(right->getWidth()),right->getAPValue());
    APFloat::cmpResult CmpRes = LHS.compare(RHS);

    bool Result = false;
    switch( fi->getPredicate() ) {
      // Predicates which only care about whether or not the operands are NaNs.
    case FCmpInst::FCMP_ORD:
      Result = (CmpRes != APFloat::cmpUnordered);
      break;

    case FCmpInst::FCMP_UNO:
      Result = (CmpRes == APFloat::cmpUnordered);
      break;

      // Ordered comparisons return false if either operand is NaN.  Unordered
      // comparisons return true if either operand is NaN.
    case FCmpInst::FCMP_UEQ:
      Result = (CmpRes == APFloat::cmpUnordered || CmpRes == APFloat::cmpEqual);
      break;
    case FCmpInst::FCMP_OEQ:
      Result = (CmpRes != APFloat::cmpUnordered && CmpRes == APFloat::cmpEqual);
      break;

    case FCmpInst::FCMP_UGT:
      Result = (CmpRes == APFloat::cmpUnordered || CmpRes == APFloat::cmpGreaterThan);
      break;
    case FCmpInst::FCMP_OGT:
      Result = (CmpRes != APFloat::cmpUnordered && CmpRes == APFloat::cmpGreaterThan);
      break;

    case FCmpInst::FCMP_UGE:
      Result = (CmpRes == APFloat::cmpUnordered || (CmpRes == APFloat::cmpGreaterThan || CmpRes == APFloat::cmpEqual));
      break;
    case FCmpInst::FCMP_OGE:
      Result = (CmpRes != APFloat::cmpUnordered && (CmpRes == APFloat::cmpGreaterThan || CmpRes == APFloat::cmpEqual));
      break;

    case FCmpInst::FCMP_ULT:
      Result = (CmpRes == APFloat::cmpUnordered || CmpRes == APFloat::cmpLessThan);
      break;
    case FCmpInst::FCMP_OLT:
      Result = (CmpRes != APFloat::cmpUnordered && CmpRes == APFloat::cmpLessThan);
      break;

    case FCmpInst::FCMP_ULE:
      Result = (CmpRes == APFloat::cmpUnordered || (CmpRes == APFloat::cmpLessThan || CmpRes == APFloat::cmpEqual));
      break;
    case FCmpInst::FCMP_OLE:
      Result = (CmpRes != APFloat::cmpUnordered && (CmpRes == APFloat::cmpLessThan || CmpRes == APFloat::cmpEqual));
      break;

    case FCmpInst::FCMP_UNE:
      Result = (CmpRes == APFloat::cmpUnordered || CmpRes != APFloat::cmpEqual);
      break;
    case FCmpInst::FCMP_ONE:
      Result = (CmpRes != APFloat::cmpUnordered && CmpRes != APFloat::cmpEqual);
      break;

    default:
      assert(0 && "Invalid FCMP predicate!");
      break;
    case FCmpInst::FCMP_FALSE:
      Result = false;
      break;
    case FCmpInst::FCMP_TRUE:
      Result = true;
      break;
    }

    bindLocal(ki, state, ConstantExpr::alloc(Result, Expr::Bool));
    break;
  }
  case Instruction::InsertValue: {
    KGEPInstruction *kgepi = static_cast<KGEPInstruction*>(ki);

    ref<Expr> agg = eval(ki, 0, state).value;
    ref<Expr> val = eval(ki, 1, state).value;

    ref<Expr> l = NULL, r = NULL;
    unsigned lOffset = kgepi->offset*8, rOffset = kgepi->offset*8 + val->getWidth();

    if (lOffset > 0)
      l = ExtractExpr::create(agg, 0, lOffset);
    if (rOffset < agg->getWidth())
      r = ExtractExpr::create(agg, rOffset, agg->getWidth() - rOffset);

    ref<Expr> result;
    if (!l.isNull() && !r.isNull())
      result = ConcatExpr::create(r, ConcatExpr::create(val, l));
    else if (!l.isNull())
      result = ConcatExpr::create(val, l);
    else if (!r.isNull())
      result = ConcatExpr::create(r, val);
    else
      result = val;

    bindLocal(ki, state, result);
    break;
  }
  case Instruction::ExtractValue: {
    KGEPInstruction *kgepi = static_cast<KGEPInstruction*>(ki);

    ref<Expr> agg = eval(ki, 0, state).value;

    ref<Expr> result = ExtractExpr::create(agg, kgepi->offset*8, getWidthForLLVMType(i->getType()));

    bindLocal(ki, state, result);
    break;
  }
  case Instruction::Fence: {
    // Ignore for now
    break;
  }
  case Instruction::InsertElement: {
    InsertElementInst *iei = cast<InsertElementInst>(i);
    ref<Expr> vec = eval(ki, 0, state).value;
    ref<Expr> newElt = eval(ki, 1, state).value;
    ref<Expr> idx = eval(ki, 2, state).value;

    ConstantExpr *cIdx = dyn_cast<ConstantExpr>(idx);
    if (cIdx == NULL) {
      terminateStateOnError(
          state, "InsertElement, support for symbolic index not implemented",
          Unhandled);
      return;
    }
    uint64_t iIdx = cIdx->getZExtValue();
    const llvm::VectorType *vt = iei->getType();
    unsigned EltBits = getWidthForLLVMType(vt->getElementType());

    if (iIdx >= vt->getNumElements()) {
      // Out of bounds write
      terminateStateOnError(state, "Out of bounds write when inserting element",
                            BadVectorAccess);
      return;
    }

    const unsigned elementCount = vt->getNumElements();
    llvm::SmallVector<ref<Expr>, 8> elems;
    elems.reserve(elementCount);
    for (unsigned i = elementCount; i != 0; --i) {
      auto of = i - 1;
      unsigned bitOffset = EltBits * of;
      elems.push_back(
          of == iIdx ? newElt : ExtractExpr::create(vec, bitOffset, EltBits));
    }

    assert(Context::get().isLittleEndian() && "FIXME:Broken for big endian");
    ref<Expr> Result = ConcatExpr::createN(elementCount, elems.data());
    bindLocal(ki, state, Result);
    break;
  }
  case Instruction::ExtractElement: {
    ExtractElementInst *eei = cast<ExtractElementInst>(i);
    ref<Expr> vec = eval(ki, 0, state).value;
    ref<Expr> idx = eval(ki, 1, state).value;

    ConstantExpr *cIdx = dyn_cast<ConstantExpr>(idx);
    if (cIdx == NULL) {
      terminateStateOnError(
          state, "ExtractElement, support for symbolic index not implemented",
          Unhandled);
      return;
    }
    uint64_t iIdx = cIdx->getZExtValue();
    const llvm::VectorType *vt = eei->getVectorOperandType();
    unsigned EltBits = getWidthForLLVMType(vt->getElementType());

    if (iIdx >= vt->getNumElements()) {
      // Out of bounds read
      terminateStateOnError(state, "Out of bounds read when extracting element",
                            BadVectorAccess);
      return;
    }

    unsigned bitOffset = EltBits * iIdx;
    ref<Expr> Result = ExtractExpr::create(vec, bitOffset, EltBits);
    bindLocal(ki, state, Result);
    break;
  }
  case Instruction::ShuffleVector:
    // Should never happen due to Scalarizer pass removing ShuffleVector
    // instructions.
    terminateStateOnExecError(state, "Unexpected ShuffleVector instruction");
    break;
  case Instruction::AtomicRMW: {
    // An atomic instruction gets a pointer and a value. It reads the value at the pointer,
    // performs its operation, stores the result and returns the value that was originally at the pointer.
    auto ai = cast<AtomicRMWInst>(i);

    auto memValWidth = getWidthForLLVMType(ai->getValOperand()->getType());

    ref<Expr> pointer = eval(ki, 0, state).value;
    ref<Expr> value = eval(ki, 1, state).value;

    auto memLoc = extractMemoryObject(state, pointer, memValWidth);
    if (!memLoc.has_value()) {
      break;
    }

    if (state.hasUnregisteredDecisions()) {
      porEventManager.registerLocal(state, addedStates, false);
    }

    porEventManager.registerLockAcquire(state, memLoc->first->getId(), false);

    auto oldValue = executeMemoryRead(state, memLoc.value(), memValWidth);
    ref<Expr> result;

    switch (ai->getOperation()) {
    case AtomicRMWInst::Xchg: {
      result = value;
      break;
    }
    case AtomicRMWInst::Add: {
      result = AddExpr::create(oldValue, value);
      break;
    }
    case AtomicRMWInst::Sub: {
      result = SubExpr::create(oldValue, value);
      break;
    }
    case AtomicRMWInst::And: {
      result = AndExpr::create(oldValue, value);;
      break;
    }
    case AtomicRMWInst::Nand: {
      result = XorExpr::create(AndExpr::create(oldValue, value), ConstantExpr::create(-1, value->getWidth()));
      break;
    }
    case AtomicRMWInst::Or: {
      result = OrExpr::create(oldValue, value);
      break;
    }
    case AtomicRMWInst::Xor: {
      result = XorExpr::create(oldValue, value);
      break;
    }
    case AtomicRMWInst::Max: {
      result = SelectExpr::create(SgtExpr::create(oldValue, value), oldValue, value);
      break;
    }
    case AtomicRMWInst::Min: {
      result = SelectExpr::create(SltExpr::create(oldValue, value), oldValue, value);
      break;
    }
    case AtomicRMWInst::UMax: {
      result = SelectExpr::create(UgtExpr::create(oldValue, value), oldValue, value);
      break;
    }
    case AtomicRMWInst::UMin: {
      result = SelectExpr::create(UltExpr::create(oldValue, value), oldValue, value);
      break;
    }
    case AtomicRMWInst::FAdd: {
      return terminateStateOnExecError(state, "Unsupported atomicrmw FAdd operation");
    }
    case AtomicRMWInst::FSub: {
      return terminateStateOnExecError(state, "Unsupported atomicrmw FSub operation");
    }
    case AtomicRMWInst::BAD_BINOP:
      return terminateStateOnExecError(state, "Bad atomicrmw operation");
    }

    // Write the new result back to the pointer
    executeMemoryWrite(state, memLoc.value(), pointer, result);

    // Every AtomicRMW returns the old value
    bindLocal(ki, state, oldValue);

    porEventManager.registerLockRelease(state, memLoc->first->getId(), true, true);
    break;
  }

  case Instruction::AtomicCmpXchg: {
    ref<Expr> pointer = eval(ki, 0, state).value;
    ref<Expr> compare = eval(ki, 1, state).value;
    ref<Expr> newValue = eval(ki, 2, state).value;

    auto atCmpXchg = cast<AtomicCmpXchgInst>(i);
    assert(atCmpXchg != nullptr);

    auto readWidth = getWidthForLLVMType(atCmpXchg->getCompareOperand()->getType());
    auto writeWidth = newValue->getWidth();

    assert(writeWidth == getWidthForLLVMType(atCmpXchg->getNewValOperand()->getType()));

    auto src = extractMemoryObject(state, pointer, std::max(readWidth, writeWidth));
    if (!src.has_value()) {
      break;
    }

    if (state.hasUnregisteredDecisions()) {
      porEventManager.registerLocal(state, addedStates, false);
    }

    porEventManager.registerLockAcquire(state, src->first->getId(), false);

    auto oldValue = executeMemoryRead(state, src.value(), readWidth);

    auto equal = EqExpr::create(oldValue, compare);
    auto write = SelectExpr::create(equal, newValue, oldValue);

    executeMemoryWrite(state, src.value(), pointer, write);

    // The return value is a struct containing the oldValue and a bool,
    // that indicates whether the replace was successful
    // -> NOTE: the original value is the first member in the struct
    //          but in the ConcatExpr it has to be the last in order to work correctly
    // FIXME: this is totally broken, but there is no easy fix at the moment
    bindLocal(ki, state, ConcatExpr::create(equal, oldValue));

    porEventManager.registerLockRelease(state, src->first->getId(), true, true);
    break;
  }

  // Other instructions...
  // Unhandled
  default:
    terminateStateOnExecError(state, "illegal instruction");
    break;
  }
}

void Executor::updateStates(ExecutionState *current) {
  if (current && std::find(removedStates.begin(), removedStates.end(), current) == removedStates.end()) {
    if (current->hasUnregisteredDecisions()) {
      porEventManager.registerLocal(*current, addedStates);
    }

    const por::configuration &cfg = current->porNode->configuration();
    if (current->needsThreadScheduling) {
      scheduleThreads(*current);
    } else {
      // If we do not need thread scheduling, then the current thread
      // must still be runnable -> we will try to execute the next instruction
      // in the current thread
      assert(current->thread().isRunnable(cfg));
    }
  }

  if (searcher) {
    searcher->update(current, addedStates, removedStates);
  }
  
  states.insert(addedStates.begin(), addedStates.end());
  addedStates.clear();

  for (std::vector<ExecutionState *>::iterator it = removedStates.begin(),
                                               ie = removedStates.end();
       it != ie; ++it) {
    ExecutionState *es = *it;
    std::set<ExecutionState*>::iterator it2 = states.find(es);
    assert(it2!=states.end());
    states.erase(it2);
    std::map<ExecutionState*, std::vector<SeedInfo> >::iterator it3 = 
      seedMap.find(es);
    if (it3 != seedMap.end())
      seedMap.erase(it3);
    processTree->remove(es->ptreeNode);
    delete es;
  }
  removedStates.clear();
}

template <typename TypeIt>
void Executor::computeOffsets(KGEPInstruction *kgepi, TypeIt ib, TypeIt ie) {
  ref<ConstantExpr> constantOffset =
    ConstantExpr::alloc(0, Context::get().getPointerWidth());
  uint64_t index = 1;
  for (TypeIt ii = ib; ii != ie; ++ii) {
    if (StructType *st = dyn_cast<StructType>(*ii)) {
      const StructLayout *sl = kmodule->targetData->getStructLayout(st);
      const ConstantInt *ci = cast<ConstantInt>(ii.getOperand());
      uint64_t addend = sl->getElementOffset((unsigned) ci->getZExtValue());
      constantOffset = constantOffset->Add(ConstantExpr::alloc(addend,
                                                               Context::get().getPointerWidth()));
    } else if (const auto set = dyn_cast<SequentialType>(*ii)) {
      uint64_t elementSize = 
        kmodule->targetData->getTypeStoreSize(set->getElementType());
      Value *operand = ii.getOperand();
      if (Constant *c = dyn_cast<Constant>(operand)) {
        ref<ConstantExpr> index =
                evalConstant(c, ExecutionState::mainThreadId)->SExt(Context::get().getPointerWidth());
        ref<ConstantExpr> addend = 
          index->Mul(ConstantExpr::alloc(elementSize,
                                         Context::get().getPointerWidth()));
        constantOffset = constantOffset->Add(addend);
      } else {
        kgepi->indices.push_back(std::make_pair(index, elementSize));
      }
#if LLVM_VERSION_CODE >= LLVM_VERSION(4, 0)
    } else if (const auto ptr = dyn_cast<PointerType>(*ii)) {
      auto elementSize =
        kmodule->targetData->getTypeStoreSize(ptr->getElementType());
      auto operand = ii.getOperand();
      if (auto c = dyn_cast<Constant>(operand)) {
        auto index = evalConstant(c, ExecutionState::mainThreadId)->SExt(Context::get().getPointerWidth());
        auto addend = index->Mul(ConstantExpr::alloc(elementSize,
                                         Context::get().getPointerWidth()));
        constantOffset = constantOffset->Add(addend);
      } else {
        kgepi->indices.push_back(std::make_pair(index, elementSize));
      }
#endif
    } else
      assert("invalid type" && 0);
    index++;
  }
  kgepi->offset = constantOffset->getZExtValue();
}

void Executor::bindInstructionConstants(KInstruction *KI) {
  if (GetElementPtrInst *gepi = dyn_cast<GetElementPtrInst>(KI->inst)) {
    KGEPInstruction *kgepi = static_cast<KGEPInstruction*>(KI);
    computeOffsets(kgepi, gep_type_begin(gepi), gep_type_end(gepi));
  } else if (InsertValueInst *ivi = dyn_cast<InsertValueInst>(KI->inst)) {
    KGEPInstruction *kgepi = static_cast<KGEPInstruction*>(KI);
    computeOffsets(kgepi, iv_type_begin(ivi), iv_type_end(ivi));
    assert(kgepi->indices.empty() && "InsertValue constant offset expected");
  } else if (ExtractValueInst *evi = dyn_cast<ExtractValueInst>(KI->inst)) {
    KGEPInstruction *kgepi = static_cast<KGEPInstruction*>(KI);
    computeOffsets(kgepi, ev_type_begin(evi), ev_type_end(evi));
    assert(kgepi->indices.empty() && "ExtractValue constant offset expected");
  }
}

void Executor::bindModuleConstants() {
  for (auto &kfp : kmodule->functions) {
    KFunction *kf = kfp.get();
    for (unsigned i=0; i<kf->numInstructions; ++i)
      bindInstructionConstants(kf->instructions[i]);
  }

  kmodule->constantTable =
      std::unique_ptr<Cell[]>(new Cell[kmodule->constants.size()]);
  for (unsigned i=0; i<kmodule->constants.size(); ++i) {
    Cell &c = kmodule->constantTable[i];
    c.value = evalConstant(kmodule->constants[i], ExecutionState::mainThreadId, nullptr);
  }
}

void Executor::checkMemoryUsage() {
  if (!MaxMemory)
    return;
  if ((stats::instructions & 0xFFFF) == 0) {
    // We need to avoid calling GetTotalMallocUsage() often because it
    // is O(elts on freelist). This is really bad since we start
    // to pummel the freelist once we hit the memory cap.
    unsigned mbs = (util::GetTotalMallocUsage() >> 20);

    if (mbs > MaxMemory) {
      if (ExitOnMaxMemory) {
        haltExecution = true;
        klee_warning("halting KLEE (over memory cap)");
      } else if (mbs > MaxMemory + 100) {
        // just guess at how many to kill
        unsigned numStates = states.size();
        unsigned toKill = std::max(1U, numStates - numStates * MaxMemory / mbs);
        klee_warning("killing %d states (over memory cap)", toKill);
        std::vector<ExecutionState *> arr(states.begin(), states.end());
        for (unsigned i = 0, N = arr.size(); N && i < toKill; ++i, --N) {
          unsigned idx = rand() % N;
          // Make two pulls to try and not hit a state that
          // covered new code.
          if (arr[idx]->coveredNew)
            idx = rand() % N;

          std::swap(arr[idx], arr[N - 1]);
          terminateStateEarly(*arr[N - 1], "Memory limit exceeded.");
        }
      }
      atMemoryLimit = true;
    } else {
      atMemoryLimit = false;
    }
  }
}

void Executor::doDumpStates() {
  if (!DumpStatesOnHalt || states.empty())
    return;

  klee_message("halting execution, dumping remaining states");
  for (const auto &state : states)
    terminateStateEarly(*state, "Execution halting.");
  updateStates(nullptr);
}

void Executor::run(ExecutionState &initialState) {
  bindModuleConstants();

  // Delay init till now so that ticks don't accrue during optimization and such.
  timers.reset();

  states.insert(&initialState);

  if (usingSeeds) {
    std::vector<SeedInfo> &v = seedMap[&initialState];
    
    for (std::vector<KTest*>::const_iterator it = usingSeeds->begin(), 
           ie = usingSeeds->end(); it != ie; ++it)
      v.push_back(SeedInfo(*it));

    int lastNumSeeds = usingSeeds->size()+10;
    time::Point lastTime, startTime = lastTime = time::getWallTime();
    ExecutionState *lastState = 0;
    while (!seedMap.empty()) {
      if (haltExecution) {
        doDumpStates();
        return;
      }

      std::map<ExecutionState*, std::vector<SeedInfo> >::iterator it = 
        seedMap.upper_bound(lastState);
      if (it == seedMap.end())
        it = seedMap.begin();
      lastState = it->first;
      ExecutionState &state = *lastState;
      KInstruction *ki = state.pc();
      stepInstruction(state);

      executeInstruction(state, ki);
      timers.invoke();
      if (::dumpStates) dumpStates();
      if (::dumpPTree) dumpPTree();
      updateStates(&state);

      if ((stats::instructions % 1000) == 0) {
        int numSeeds = 0, numStates = 0;
        for (std::map<ExecutionState*, std::vector<SeedInfo> >::iterator
               it = seedMap.begin(), ie = seedMap.end();
             it != ie; ++it) {
          numSeeds += it->second.size();
          numStates++;
        }
        const auto time = time::getWallTime();
        const time::Span seedTime(SeedTime);
        if (seedTime && time > startTime + seedTime) {
          klee_warning("seed time expired, %d seeds remain over %d states",
                       numSeeds, numStates);
          break;
        } else if (numSeeds<=lastNumSeeds-10 ||
                   time - lastTime >= time::seconds(10)) {
          lastTime = time;
          lastNumSeeds = numSeeds;          
          klee_message("%d seeds remaining over: %d states", 
                       numSeeds, numStates);
        }
      }
    }

    klee_message("seeding done (%d states remain)", (int) states.size());

    if (OnlySeed) {
      doDumpStates();
      return;
    }
  }

  searcher = constructUserSearcher(*this);

  std::vector<ExecutionState *> newStates(states.begin(), states.end());
  searcher->update(0, newStates, std::vector<ExecutionState *>());

  bool firstInstruction = true;

  while (!states.empty() && !haltExecution) {
    ExecutionState &state = searcher->selectState();
    KInstruction *ki = state.pc();

    // we will execute a new instruction and therefore we have to reset the flag
    stepInstruction(state);

    executeInstruction(state, ki);
    timers.invoke();
    if (firstInstruction && statesJSONFile) {
      (*statesJSONFile) << "    \"functionlists_length\": "
                        << state.memoryState.getFunctionListsLength() << ",\n";
      (*statesJSONFile) << "    \"functionlists_capacity\": "
                        << state.memoryState.getFunctionListsCapacity() << "\n";
      (*statesJSONFile) << "  }";
    }
    updateStatesJSON(ki, state);

    if (::dumpStates) dumpStates();
    if (::dumpPTree) dumpPTree();

    checkMemoryUsage();

    updateStates(&state);

    if (stats::instructions % 10000 == 0) {
      exploreSchedules(state);
      updateStates(nullptr);
    }

    firstInstruction = false;
  }

  delete searcher;
  searcher = nullptr;

  doDumpStates();
}

void Executor::exploreSchedules(ExecutionState &state, bool maximalConfiguration) {
  if (!ExploreSchedules || !state.porNode || !state.porNode->parent()) {
    return;
  }
  por::configuration const& cfg = state.porNode->configuration();

  std::vector<const por::event::event *> conflicting_extensions = cfg.conflicting_extensions(true);

  if (maximalConfiguration) {
    for (auto &[tid, thread] : state.threads) {
      if (thread.isRunnable(cfg)) {
        continue; // FIXME: incompleteness
      }
      if (thread.state == ThreadState::Waiting) {
        por::event::lock_id_t lid;
        por::event::event_kind kind;

        if (auto lock = thread.isWaitingOn<Thread::wait_lock_t>()) {
          lid = lock->lock;
          kind = por::event::event_kind::lock_acquire;
        } else if (auto wait = thread.isWaitingOn<Thread::wait_cv_2_t>()) {
          lid = wait->lock;
          kind = por::event::event_kind::wait2;
        } else {
          continue;
        }

        auto dlcex = cfg.conflicting_extensions_deadlock(tid, lid, kind, true);
        conflicting_extensions.insert(conflicting_extensions.end(), dlcex.begin(), dlcex.end());
      }
    }
  }

  for (por::event::event const *cex : conflicting_extensions) {
    assert(!cex->is_cutoff());
    if (MaxContextSwitchDegree && por::is_above_csd_limit(*cex, MaxContextSwitchDegree)) {
      //klee_warning("Context Switch Degree of conflicting extension above limit.");
      cfg.unfolding()->remove_event(*cex);
    }
  }

  std::vector<por::node*> branch(state.porNode->parent()->branch_begin(), state.porNode->parent()->branch_end());
  branch.pop_back(); // remove root node
  auto leaves = por::node::create_right_branches(branch);

  for (auto const &l : leaves) {
    ExecutionState *toExecute = new ExecutionState(l);

    registerFork(state, toExecute);
    addedStates.push_back(toExecute);

    // thread of last event may not be runnable or lead to wrong event
    toExecute->needsThreadScheduling = true;
    scheduleThreads(*toExecute);

    if (DebugAlternatives) {
      llvm::errs() << "leaf (state id: " << toExecute->id << "): " << l.start->to_string();
      llvm::errs() << "catch-up:\n";
      for (auto const *e : l.catch_up) {
        llvm::errs() << "  " << e->to_string(true) << "\n";
      }
      llvm::errs() << "\n";
    }
  }

  if (maximalConfiguration && !state.porNode->has_children()) {
    state.porNode->backtrack();
  }
}

void Executor::updateStatesJSON(KInstruction *ki, const ExecutionState &state,
                                std::string ktest, std::string error) {
  if (statesJSONFile) {
    auto time = std::chrono::steady_clock::now() - executorStartTime;
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(time);
    auto milliseconds =
      std::chrono::duration_cast<std::chrono::milliseconds>(time) - seconds;

    static size_t lastStateId = 0;

    if (lastStateId != state.id
        || !ktest.empty()
        || !error.empty()
    ) {
      (*statesJSONFile) << ",\n  {\n";
      (*statesJSONFile) << "    \"state_id\": " << state.id << ",\n";
      if (!ktest.empty()) {
        (*statesJSONFile) << "    \"ktest\": \"" << ktest << "\",\n";
      }
      if (!error.empty()) {
        (*statesJSONFile) << "    \"error\": \"" << error << "\",\n";
      }
      (*statesJSONFile) << "    \"heap\": " << util::GetTotalMallocUsage()
                        << ",\n";
      (*statesJSONFile) << "    \"timestamp\": " << seconds.count()
                        << "." << milliseconds.count() << ",\n";
      if (ki != nullptr) {
        (*statesJSONFile) << "    \"instructions\": " << stats::instructions
                          << ",\n";
        (*statesJSONFile) << "    \"instruction_id\": " << ki->info->id << "\n";
      } else {
        (*statesJSONFile) << "    \"instructions\": " << stats::instructions
                          << "\n";
      }
      (*statesJSONFile) << "  }";

      lastStateId = state.id;
    }
  }
}

void Executor::updateForkJSON(const ExecutionState &current,
                              const ExecutionState &trueState,
                              const ExecutionState &falseState) {
  static bool started = false;

  if (forkJSONFile) {
    auto time = std::chrono::steady_clock::now() - executorStartTime;
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(time);
    auto milliseconds =
      std::chrono::duration_cast<std::chrono::milliseconds>(time) - seconds;

    if (!started) {
      (*forkJSONFile) << "[\n";
      (*forkJSONFile) << "  {\n";
      started = true;
    } else {
      (*forkJSONFile) << ",\n  {\n";
    }
    (*forkJSONFile) << "    \"state_id\": " << current.id << ",\n";
    if (trueState.id == falseState.id) {
      (*forkJSONFile) << "    \"new_id\": " << trueState.id << ",\n";
    } else {
      (*forkJSONFile) << "    \"true_id\": " << trueState.id << ",\n";
      (*forkJSONFile) << "    \"false_id\": " << falseState.id << ",\n";
    }
    (*forkJSONFile) << "    \"timestamp\": " << seconds.count()
                    << "." << milliseconds.count() << ",\n";
    (*forkJSONFile) << "    \"instructions\": " << stats::instructions << "\n";
    (*forkJSONFile) << "  }";
  }
}

std::string Executor::getAddressInfo(ExecutionState &state, 
                                     ref<Expr> address) const{
  std::string Str;
  llvm::raw_string_ostream info(Str);
  info << "\taddress: " << address << "\n";
  uint64_t example;
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(address)) {
    example = CE->getZExtValue();
  } else {
    ref<ConstantExpr> value;
    bool success = solver->getValue(state, address, value);
    assert(success && "FIXME: Unhandled solver failure");
    (void) success;
    example = value->getZExtValue();
    info << "\texample: " << example << "\n";
    std::pair< ref<Expr>, ref<Expr> > res = solver->getRange(state, address);
    info << "\trange: [" << res.first << ", " << res.second <<"]\n";
  }
  
  MemoryObject hack((unsigned) example);    
  MemoryMap::iterator lower = state.addressSpace.objects.upper_bound(&hack);
  info << "\tnext: ";
  if (lower==state.addressSpace.objects.end()) {
    info << "none\n";
  } else {
    const MemoryObject *mo = lower->first;
    std::string alloc_info;
    mo->getAllocInfo(alloc_info);
    info << "object at " << mo->address
         << " of size " << mo->size << "\n"
         << "\t\t" << alloc_info << "\n";
  }
  if (lower!=state.addressSpace.objects.begin()) {
    --lower;
    info << "\tprev: ";
    if (lower==state.addressSpace.objects.end()) {
      info << "none\n";
    } else {
      const MemoryObject *mo = lower->first;
      std::string alloc_info;
      mo->getAllocInfo(alloc_info);
      info << "object at " << mo->address 
           << " of size " << mo->size << "\n"
           << "\t\t" << alloc_info << "\n";
    }
  }

  return info.str();
}


void Executor::terminateStateSilently(ExecutionState &state) {
  auto it = std::find(addedStates.begin(), addedStates.end(), &state);

  if (it == addedStates.end()) {
    Thread &thread = state.thread();
    thread.pc = thread.prevPc;
    auto it2 __attribute__ ((unused)) = std::find(removedStates.begin(),
                                                  removedStates.end(), &state);
    assert(it2 == removedStates.end() && "May not add a state double times");

    removedStates.push_back(&state);
  } else {
    // never reached searcher, just delete immediately
    auto it3 = seedMap.find(&state);
    if (it3 != seedMap.end())
      seedMap.erase(it3);
    addedStates.erase(it);
    processTree->remove(state.ptreeNode);
    delete &state;
  }
}

void Executor::terminateState(ExecutionState &state) {
  if (replayKTest && replayPosition!=replayKTest->numObjects) {
    klee_warning_once(replayKTest,
                      "replay did not consume all objects in test input.");
  }

  interpreterHandler->incPathsExplored();

  exploreSchedules(state, true);

  terminateStateSilently(state);
}

void Executor::terminateStateEarly(ExecutionState &state,
                                   const Twine &message) {
  std::string ktest = "";
  if (!OnlyOutputStatesCoveringNew || state.coveredNew ||
      (AlwaysOutputSeeds && seedMap.count(&state)))
    ktest = interpreterHandler->processTestCase(state,
                                                (message + "\n").str().c_str(),
                                                "early");
  updateStatesJSON(nullptr, state, ktest, "early");
  terminateState(state);
}

void Executor::terminateStateOnExit(ExecutionState &state) {
  std::string ktest = "";
  if (!OnlyOutputStatesCoveringNew || state.coveredNew || 
      (AlwaysOutputSeeds && seedMap.count(&state)))
    ktest = interpreterHandler->processTestCase(state, 0, 0);

  updateStatesJSON(nullptr, state, ktest);
  ++stats::maxConfigurations;
  terminateState(state);
}

const InstructionInfo & Executor::getLastNonKleeInternalInstruction(const ExecutionState &state,
    Instruction ** lastInstruction) {
  const InstructionInfo * ii = 0;

  if (state.threadState() != ThreadState::Exited) {
    // unroll the stack of the applications state and find
    // the last instruction which is not inside a KLEE internal function
    auto it = state.stack().rbegin(), itE = state.stack().rend();

    // don't check beyond the outermost function (i.e. main())
    itE--;

    if (kmodule->internalFunctions.count(it->kf->function) == 0) {
      ii = state.prevPc()->info;
      *lastInstruction = state.prevPc()->inst;
      //  Cannot return yet because even though
      //  it->function is not an internal function it might of
      //  been called from an internal function.
    }

    // Wind up the stack and check if we are in a KLEE internal function.
    // We visit the entire stack because we want to return a CallInstruction
    // that was not reached via any KLEE internal functions.
    for (; it != itE; ++it) {
      // check calling instruction and if it is contained in a KLEE internal function
      const Function *f = (*it->caller).inst->getParent()->getParent();
      if (kmodule->internalFunctions.count(f)) {
        ii = 0;
        continue;
      }
      if (!ii) {
        ii = (*it->caller).info;
        *lastInstruction = (*it->caller).inst;
      }
    }
  }

  if (!ii) {
    // something went wrong, play safe and return the current instruction info
    *lastInstruction = state.prevPc()->inst;
    return *state.prevPc()->info;
  }
  return *ii;
}

bool Executor::shouldExitOn(enum TerminateReason termReason) {
  std::vector<TerminateReason>::iterator s = ExitOnErrorType.begin();
  std::vector<TerminateReason>::iterator e = ExitOnErrorType.end();

  for (; s != e; ++s)
    if (termReason == *s)
      return true;

  return false;
}

void Executor::terminateStateOnError(ExecutionState &state,
                                     const llvm::Twine &messaget,
                                     enum TerminateReason termReason,
                                     const char *suffix,
                                     const llvm::Twine &info) {
  auto timeToError = std::chrono::steady_clock::now() - executorStartTime;

  std::string message = messaget.str();
  static std::set< std::pair<Instruction*, std::string> > emittedErrors;
  Instruction * lastInst;
  const InstructionInfo &ii = getLastNonKleeInternalInstruction(state, &lastInst);
  std::string ktest = "";
  
  if (EmitAllErrors ||
      emittedErrors.insert(std::make_pair(lastInst, message)).second) {
    if (ii.file != "") {
      klee_message("ERROR: %s:%d: %s", ii.file.c_str(), ii.line, message.c_str());
    } else {
      klee_message("ERROR: (location information missing) %s", message.c_str());
    }
    if (!EmitAllErrors)
      klee_message("NOTE: now ignoring this error at this location");

    std::string MsgString;
    llvm::raw_string_ostream msg(MsgString);
    msg << "Error: " << message << "\n";
    if (ii.file != "") {
      msg << "File: " << ii.file << "\n";
      msg << "Line: " << ii.line << "\n";
      msg << "assembly.ll line: " << ii.assemblyLine << "\n";
    }
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(timeToError);
    auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(timeToError) - seconds;
    msg << "Time to error: " << seconds.count() << "." << milliseconds.count() << " seconds\n";
    msg << "Stack: \n";
    state.dumpStack(msg);

    std::string info_str = info.str();
    if (!info_str.empty())
      msg << "Info: \n" << info_str;

    std::string suffix_buf;
    if (!suffix) {
      suffix_buf = TerminateReasonNames[termReason];
      suffix_buf += ".err";
      suffix = suffix_buf.c_str();
    }

    ktest = interpreterHandler->processTestCase(state,
                                                msg.str().c_str(),
                                                suffix);
  }

  updateStatesJSON(nullptr, state, ktest, TerminateReasonNames[termReason]);
  terminateState(state);

  if (shouldExitOn(termReason))
    haltExecution = true;
}

void Executor::callExternalFunction(ExecutionState &state,
                                    KInstruction *target,
                                    Function *function,
                                    std::vector< ref<Expr> > &arguments) {
  // check if specialFunctionHandler wants it
  if (specialFunctionHandler->handle(state, function, target, arguments))
    return;

  if (ExternalCalls == ExternalCallPolicy::None) {
    klee_warning("Disallowed call to external function: %s\n",
               function->getName().str().c_str());
    terminateStateOnError(state, "external calls disallowed", User);
    return;
  }

  // normal external function handling path
  // allocate 128 bits for each argument (+return value) to support fp80's;
  // we could iterate through all the arguments first and determine the exact
  // size we need, but this is faster, and the memory usage isn't significant.
  uint64_t *args = (uint64_t*) alloca(2*sizeof(*args) * (arguments.size() + 1));
  memset(args, 0, 2 * sizeof(*args) * (arguments.size() + 1));
  unsigned wordIndex = 2;
  for (std::vector<ref<Expr> >::iterator ai = arguments.begin(), 
       ae = arguments.end(); ai!=ae; ++ai) {
    if (ExternalCalls == ExternalCallPolicy::All) { // don't bother checking uniqueness
      *ai = optimizer.optimizeExpr(*ai, true);
      ref<ConstantExpr> ce;
      bool success = solver->getValue(state, *ai, ce);
      assert(success && "FIXME: Unhandled solver failure");
      (void) success;
      ce->toMemory(&args[wordIndex]);
      ObjectPair op;
      // Checking to see if the argument is a pointer to something
      if (ce->getWidth() == Context::get().getPointerWidth() &&
          state.addressSpace.resolveOne(ce, op)) {
        op.second->flushToConcreteStore(solver, state);
      }
      wordIndex += (ce->getWidth()+63)/64;
    } else {
      ref<Expr> arg = toUnique(state, *ai);
      if (ConstantExpr *ce = dyn_cast<ConstantExpr>(arg)) {
        // XXX kick toMemory functions from here
        ce->toMemory(&args[wordIndex]);
        wordIndex += (ce->getWidth()+63)/64;
      } else {
        terminateStateOnExecError(state, 
                                  "external call with symbolic argument: " + 
                                  function->getName());
        return;
      }
    }
  }

  // Prepare external memory for invoking the function
  state.addressSpace.copyOutConcretes();
#ifndef WINDOWS
  // Update external errno state with local state value
  const ObjectState* errnoOs = state.addressSpace.findObject(state.errnoMo());

  ref<Expr> errValueExpr = errnoOs->read(0, state.errnoMo()->size * 8);
  ConstantExpr *errnoValue = dyn_cast<ConstantExpr>(errValueExpr);
  if (!errnoValue) {
    terminateStateOnExecError(state,
                              "external call with errno value symbolic: " +
                                  function->getName());
    return;
  }

  externalDispatcher->setLastErrno(
      errnoValue->getZExtValue(state.errnoMo()->size * 8));
#endif

  if (!SuppressExternalWarnings) {

    std::string TmpStr;
    llvm::raw_string_ostream os(TmpStr);
    os << "calling external: " << function->getName().str() << "(";
    for (unsigned i=0; i<arguments.size(); i++) {
      os << arguments[i];
      if (i != arguments.size()-1)
        os << ", ";
    }

    os << ") at " << state.pc()->getSourceLocation();
    
    if (AllExternalWarnings)
      klee_warning("%s", os.str().c_str());
    else
      klee_warning_once(function, "%s", os.str().c_str());
  }

  bool success = externalDispatcher->executeCall(function, target->inst, args);
  if (!success) {
    terminateStateOnError(state, "failed external call: " + function->getName(),
                          External);
    return;
  }

  bool failure = state.addressSpace.checkChangedConcreteObjects(
    [this,&state](const MemoryObject& mo, const auto* store) -> bool {
      // So we already know that the object was modified, now check each byte
      // range for actual changes
      auto address = reinterpret_cast<std::uint8_t*>(mo.address);

      for (std::size_t i = 0; i < mo.size; i++) {
        if (address[i] == store[i]) {
          continue;
        }

        // We found the first changed byte, now check for the first
        // byte that did not change or once the size is reached
        auto end = i + 1;
        while (end < mo.size && address[end] != store[end]) {
          end++;
        }

        bool safe = processMemoryAccess(
          state,
          &mo,
          ConstantExpr::alloc(i, 64),
          end - i,
          MemoryOperation::Type::WRITE
        );

        if (!safe) {
          return true;
        }
      }

      return false;
    }
  );

  if (failure) {
    return;
  }

  if (!state.addressSpace.copyInConcretes(state)) {
    terminateStateOnError(state, "external modified read-only object",
                          External);
    return;
  }

  memory->markMemoryRegionsAsUnneeded();

#ifndef WINDOWS
  // Update errno memory object with the errno value from the call
  int error = externalDispatcher->getLastErrno();
  state.addressSpace.copyInConcrete(state, state.errnoMo(), errnoOs,
                                    (uint64_t)&error);
#endif

  // there is no new stack frame for external functions and thus no return
  // hence we have to immediately leave any function that is external call
  if (PruneStates) {
    state.memoryState.registerFunctionRet(function);
  }

  Type *resultType = target->inst->getType();
  if (resultType != Type::getVoidTy(function->getContext())) {
    ref<Expr> e = ConstantExpr::fromMemory((void*) args, 
                                           getWidthForLLVMType(resultType));
    bindLocal(target, state, e);
  }
}

/***/

ref<Expr> Executor::replaceReadWithSymbolic(ExecutionState &state, 
                                            ref<Expr> e) {
  unsigned n = interpreterOpts.MakeConcreteSymbolic;
  if (!n || replayKTest || replayPath)
    return e;

  // right now, we don't replace symbolics (is there any reason to?)
  if (!isa<ConstantExpr>(e))
    return e;

  if (n != 1 && random() % n)
    return e;

  // create a new fresh location, assert it is equal to concrete value in e
  // and return it.
  
  static unsigned id;
  const Array *array =
      arrayCache.CreateArray("rrws_arr" + llvm::utostr(++id),
                             Expr::getMinBytesForWidth(e->getWidth()));
  ref<Expr> res = Expr::createTempRead(array, e->getWidth());
  ref<Expr> eq = NotOptimizedExpr::create(EqExpr::create(e, res));
  llvm::errs() << "Making symbolic: " << eq << "\n";
  addConstraint(state, eq);
  return res;
}

ObjectState *Executor::bindObjectInState(ExecutionState &state, 
                                         const MemoryObject *mo,
                                         bool isLocal,
                                         const Array *array) {
  ObjectState *os = array ? new ObjectState(mo, array) : new ObjectState(mo);
  state.addressSpace.bindObject(mo, os);

  // Its possible that multiple bindings of the same mo in the state
  // will put multiple copies on this list, but it doesn't really
  // matter because all we use this list for is to unbind the object
  // on function return.
  if (isLocal) {
    state.thread().stack.back().allocas.push_back(mo);
  }

  return os;
}

void Executor::executeAlloc(ExecutionState &state,
                            ref<Expr> size,
                            bool isLocal,
                            KInstruction *target,
                            bool zeroMemory,
                            const ObjectState *reallocFrom,
                            size_t allocationAlignment) {
  size = toUnique(state, size);
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(size)) {
    const llvm::Value *allocSite = state.prevPc()->inst;
    if (allocationAlignment == 0) {
      allocationAlignment = getAllocationAlignment(allocSite);
    }
    MemoryObject *mo =
        memory->allocate(CE->getZExtValue(), isLocal,
                         allocSite, state.thread(),
                         state.stackFrameIndex(),
                         allocationAlignment);
    if (!mo) {
      bindLocal(target, state, 
                ConstantExpr::alloc(0, Context::get().getPointerWidth()));
    } else {
      processMemoryAccess(state, mo, nullptr, 0, MemoryOperation::Type::ALLOC);

      ObjectState *os = bindObjectInState(state, mo, isLocal);
      if (zeroMemory) {
        os->initializeToZero();
      } else {
        os->initializeToRandom();
      }

      bindLocal(target, state, mo->getBaseExpr());
      
      if (reallocFrom) {
        unsigned count = std::min(reallocFrom->size, os->size);
        for (unsigned i=0; i<count; i++) {
          os->write(i, reallocFrom->read8(i));
        }

        // free previous allocation
        const MemoryObject *reallocatedObject = reallocFrom->getObject();

        processMemoryAccess(state, reallocatedObject, nullptr, 0, MemoryOperation::Type::FREE);

        if (PruneStates) {
          state.memoryState.unregisterWrite(*reallocatedObject, *reallocFrom);
        }

        reallocatedObject->parent->deallocate(reallocatedObject, state.thread());
        state.addressSpace.unbindObject(reallocatedObject);
      }

      if (PruneStates) {
        // after realloc to let copied byted overwrite initialization
        state.memoryState.registerWrite(*mo, *os);
      }
    }
  } else {
    // XXX For now we just pick a size. Ideally we would support
    // symbolic sizes fully but even if we don't it would be better to
    // "smartly" pick a value, for example we could fork and pick the
    // min and max values and perhaps some intermediate (reasonable
    // value).
    // 
    // It would also be nice to recognize the case when size has
    // exactly two values and just fork (but we need to get rid of
    // return argument first). This shows up in pcre when llvm
    // collapses the size expression with a select.

    size = optimizer.optimizeExpr(size, true);

    ref<ConstantExpr> example;
    bool success = solver->getValue(state, size, example);
    assert(success && "FIXME: Unhandled solver failure");
    (void) success;
    
    // Try and start with a small example.
    Expr::Width W = example->getWidth();
    while (example->Ugt(ConstantExpr::alloc(128, W))->isTrue()) {
      ref<ConstantExpr> tmp = example->LShr(ConstantExpr::alloc(1, W));
      bool res;
      bool success = solver->mayBeTrue(state, EqExpr::create(tmp, size), res);
      assert(success && "FIXME: Unhandled solver failure");      
      (void) success;
      if (!res)
        break;
      example = tmp;
    }

    StatePair fixedSize = fork(state, EqExpr::create(example, size), true);
    
    if (fixedSize.second) { 
      // Check for exactly two values
      ref<ConstantExpr> tmp;
      bool success = solver->getValue(*fixedSize.second, size, tmp);
      assert(success && "FIXME: Unhandled solver failure");      
      (void) success;
      bool res;
      success = solver->mustBeTrue(*fixedSize.second, 
                                   EqExpr::create(tmp, size),
                                   res);
      assert(success && "FIXME: Unhandled solver failure");      
      (void) success;
      if (res) {
        if (fixedSize.second != &state) {
          // local event after fork() is only added after executeInstruction() has finished
          // for the purpose of data race detection, temporarily set porNode of new state
          assert(fixedSize.second->porNode == nullptr);
          fixedSize.second->porNode = state.porNode;
        }

        executeAlloc(*fixedSize.second, tmp, isLocal,
                     target, zeroMemory, reallocFrom);

        if (fixedSize.second != &state) {
          // reset porNode to be updated after executeInstruction()
          fixedSize.second->porNode = nullptr;
        }
      } else {
        // See if a *really* big value is possible. If so assume
        // malloc will fail for it, so lets fork and return 0.
        StatePair hugeSize = 
          fork(*fixedSize.second, 
               UltExpr::create(ConstantExpr::alloc(1U<<31, W), size),
               true);
        if (hugeSize.first) {
          klee_message("NOTE: found huge malloc, returning 0");
          bindLocal(target, *hugeSize.first, 
                    ConstantExpr::alloc(0, Context::get().getPointerWidth()));
        }
        
        if (hugeSize.second) {

          std::string Str;
          llvm::raw_string_ostream info(Str);
          ExprPPrinter::printOne(info, "  size expr", size);
          info << "  concretization : " << example << "\n";
          info << "  unbound example: " << tmp << "\n";
          terminateStateOnError(*hugeSize.second, "concretized symbolic size",
                                Model, NULL, info.str());
        }
      }
    }

    if (fixedSize.first) // can be zero when fork fails
      executeAlloc(*fixedSize.first, example, isLocal, 
                   target, zeroMemory, reallocFrom);
  }
}

void Executor::executeFree(ExecutionState &state,
                           ref<Expr> address,
                           KInstruction *target) {
  address = optimizer.optimizeExpr(address, true);
  StatePair zeroPointer = fork(state, Expr::createIsZero(address), true);
  if (zeroPointer.first) {
    if (target)
      bindLocal(target, *zeroPointer.first, Expr::createPointer(0));
  }
  if (zeroPointer.second) { // address != 0
    ExactResolutionList rl;
    resolveExact(*zeroPointer.second, address, rl, "free");
    
    for (Executor::ExactResolutionList::iterator it = rl.begin(), 
           ie = rl.end(); it != ie; ++it) {
      const MemoryObject *mo = it->first.first;
      const ObjectState *os = it->first.second;
      if (mo->isLocal) {
        terminateStateOnError(*it->second, "free of alloca", Free, NULL,
                              getAddressInfo(*it->second, address));
      } else if (mo->isGlobal) {
        terminateStateOnError(*it->second, "free of global", Free, NULL,
                              getAddressInfo(*it->second, address));
      } else {
        if (it->second != &state) {
          // local event after fork() is only added after executeInstruction() has finished
          // for the purpose of data race detection, temporarily set porNode of new state
          assert(it->second->porNode == nullptr);
          it->second->porNode = state.porNode;
        }

        // A free operation should be tracked as well
        processMemoryAccess(*it->second, mo, nullptr, 0, MemoryOperation::Type::FREE);

        if (it->second != &state) {
          // reset porNode to be updated after executeInstruction()
          it->second->porNode = nullptr;
        }

        if (PruneStates)
          it->second->memoryState.unregisterWrite(*mo, *os);

        auto thread = state.getThreadById(mo->getAllocationStackFrame().first);
        assert(thread.has_value() && "MemoryObject created by thread that is not known");

        mo->parent->deallocate(mo, thread.value().get());
        it->second->addressSpace.unbindObject(mo);

        if (target)
          bindLocal(target, *it->second, Expr::createPointer(0));
      }
    }
  }
}

void Executor::resolveExact(ExecutionState &state,
                            ref<Expr> p,
                            ExactResolutionList &results, 
                            const std::string &name) {
  p = optimizer.optimizeExpr(p, true);
  // XXX we may want to be capping this?
  ResolutionList rl;
  state.addressSpace.resolve(state, solver, p, rl);
  
  ExecutionState *unbound = &state;
  for (ResolutionList::iterator it = rl.begin(), ie = rl.end(); 
       it != ie; ++it) {
    ref<Expr> inBounds = EqExpr::create(p, it->first->getBaseExpr());
    
    StatePair branches = fork(*unbound, inBounds, true);
    
    if (branches.first)
      results.push_back(std::make_pair(*it, branches.first));

    unbound = branches.second;
    if (!unbound) // Fork failure
      break;
  }

  if (unbound) {
    terminateStateOnError(*unbound, "memory error: invalid pointer: " + name,
                          Ptr, NULL, getAddressInfo(*unbound, p));
  }
}

std::optional<Executor::MemoryLocation>
Executor::extractMemoryObject(ExecutionState &state, ref<Expr> address, Expr::Width bitWidth) {
  auto bytes = Expr::getMinBytesForWidth(bitWidth);

  if (SimplifySymIndices && !isa<ConstantExpr>(address)) {
    address = state.constraints.simplifyExpr(address);
  }

  address = optimizer.optimizeExpr(address, true);

  // fast path: single in-bounds resolution
  ObjectPair op;
  bool success;
  solver->setTimeout(coreSolverTimeout);
  if (!state.addressSpace.resolveOne(state, solver, address, op, success)) {
    address = toConstant(state, address, "resolveOne failure");
    success = state.addressSpace.resolveOne(cast<ConstantExpr>(address), op);
  }
  solver->setTimeout(time::Span());

  if (success) {
    const MemoryObject *mo = op.first;

    if (MaxSymArraySize && mo->size >= MaxSymArraySize) {
      address = toConstant(state, address, "max-sym-array-size");
    }

    ref<Expr> offset = mo->getOffsetExpr(address);
    ref<Expr> check = mo->getBoundsCheckOffset(offset, bytes);
    check = optimizer.optimizeExpr(check, true);

    bool inBounds;
    solver->setTimeout(coreSolverTimeout);
    success = solver->mustBeTrue(state, check, inBounds);
    solver->setTimeout(time::Span());

    if (!success) {
      state.thread().pc = state.thread().prevPc;
      terminateStateEarly(state, "Query timed out (bounds check).");
      return {};
    }

    if (inBounds) {
      return std::make_pair(mo, offset);
    }
  }

  // we are on an error path (no resolution, multiple resolution, one
  // resolution with out of bounds)

  address = optimizer.optimizeExpr(address, true);
  ResolutionList rl;
  solver->setTimeout(coreSolverTimeout);
  bool incomplete = state.addressSpace.resolve(state, solver, address, rl,
                                               0, coreSolverTimeout);
  solver->setTimeout(time::Span());

  // XXX there is some query wasteage here. who cares?
  ExecutionState *unbound = &state;
  std::optional<MemoryLocation> result;

  for (const auto& i : rl) {
    const MemoryObject *mo = i.first;
    ref<Expr> inBounds = mo->getBoundsCheckPointer(address, bytes);

    StatePair branches = fork(*unbound, inBounds, true);
    ExecutionState *bound = branches.first;

    // bound can be 0 on failure or overlapped
    if (!result.has_value() && bound == &state) {
      result = std::make_pair(mo, mo->getOffsetExpr(address));
    }

    unbound = branches.second;
    if (unbound) {
      // Reset current pc since the operation has to be redone in the forked state
      unbound->thread().pc = unbound->thread().prevPc;
    } else {
      break;
    }
  }

  // XXX should we distinguish out of bounds and overlapped cases?
  if (unbound) {
    if (incomplete) {
      terminateStateEarly(*unbound, "Query timed out (resolve).");
    } else {
      terminateStateOnError(*unbound, "memory error: out of bound pointer", Ptr,
                            nullptr, getAddressInfo(*unbound, address));
    }
  }

  return result;
}

void Executor::executeMemoryWrite(ExecutionState& state,
                                  const MemoryLocation& memLoc,
                                  ref<Expr> address,
                                  ref<Expr> value) {
  auto bytes = Expr::getMinBytesForWidth(value->getWidth());

  if (SimplifySymIndices && !isa<ConstantExpr>(value)) {
    value = state.constraints.simplifyExpr(value);
  }

  auto mo = memLoc.first;
  auto& offset = memLoc.second;
  auto os = state.addressSpace.findObject(mo);

  assert(os != nullptr);
  if (os->readOnly) {
    terminateStateOnError(state, "memory error: object read only", ReadOnly);
    return;
  }

  processMemoryAccess(state, mo, offset, bytes, MemoryOperation::Type::WRITE);

  auto* wos = state.addressSpace.getWriteable(mo, os);

  if (PruneStates) {
    // unregister previous value to avoid cancellation
    state.memoryState.unregisterWrite(address, *mo, *wos, bytes);
  }

  wos->write(offset, value);

  if (PruneStates) {
    state.memoryState.registerWrite(address, *mo, *wos, bytes);
  }
}

ref<Expr> Executor::executeMemoryRead(ExecutionState& state,
                                 const MemoryLocation& memLoc,
                                 Expr::Width bitWidth) {
  auto bytes = Expr::getMinBytesForWidth(bitWidth);

  auto mo = memLoc.first;
  auto& offset = memLoc.second;

  auto os = state.addressSpace.findObject(mo);
  assert(os != nullptr);

  ref<Expr> result = os->read(offset, bitWidth);
  processMemoryAccess(state, mo, offset, bytes, MemoryOperation::Type::READ);

  if (interpreterOpts.MakeConcreteSymbolic) {
    result = replaceReadWithSymbolic(state, result);
  }

  return result;
}

void Executor::executeMemoryOperation(ExecutionState &state,
                                      bool isWrite,
                                      ref<Expr> address,
                                      ref<Expr> value /* undef if read */,
                                      KInstruction *target /* undef if write */) {

  Expr::Width width = 0;
  if (isWrite) {
    width = value->getWidth();
  } else {
    width = getWidthForLLVMType(target->inst->getType());
  }

  auto memRegion = extractMemoryObject(state, address, width);
  if (!memRegion.has_value()) {
    return;
  }

  if (state.hasUnregisteredDecisions()) {
    porEventManager.registerLocal(state, addedStates, false);
  }

  if (isWrite) {
    executeMemoryWrite(state, memRegion.value(), address, value);
  } else {
    auto res = executeMemoryRead(state, memRegion.value(), width);
    bindLocal(target, state, res);
  }
}

void Executor::executeMakeSymbolic(ExecutionState &state,
                                   ref<Expr> address,
                                   const MemoryObject *mo,
                                   const ObjectState *os,
                                   const std::string &name) {
  if (PruneStates)
    state.memoryState.unregisterWrite(*mo, *os);

  ObjectState *newOs;

  // Create a new object state for the memory object (instead of a copy).
  if (!replayKTest) {

    // Find a unique name for this array.  First try the original name,
    // or if that fails try adding a unique identifier.
    unsigned id = 0;
    std::string uniqueName = name;
    while (!state.arrayNames.insert(uniqueName).second) {
      uniqueName = name + "_" + llvm::utostr(++id);
    }
    const Array *array = arrayCache.CreateArray(uniqueName, mo->size);
    newOs = bindObjectInState(state, mo, false, array);
    state.addSymbolic(mo, array);
    
    std::map< ExecutionState*, std::vector<SeedInfo> >::iterator it = 
      seedMap.find(&state);
    if (it!=seedMap.end()) { // In seed mode we need to add this as a
                             // binding.
      for (std::vector<SeedInfo>::iterator siit = it->second.begin(), 
             siie = it->second.end(); siit != siie; ++siit) {
        SeedInfo &si = *siit;
        KTestObject *obj = si.getNextInput(mo, NamedSeedMatching);

        if (!obj) {
          if (ZeroSeedExtension) {
            std::vector<unsigned char> &values = si.assignment.bindings[array];
            values = std::vector<unsigned char>(mo->size, '\0');
          } else if (!AllowSeedExtension) {
            terminateStateOnError(state, "ran out of inputs during seeding",
                                  User);
            return;
          }
        } else {
          if (obj->numBytes != mo->size &&
              ((!(AllowSeedExtension || ZeroSeedExtension)
                && obj->numBytes < mo->size) ||
               (!AllowSeedTruncation && obj->numBytes > mo->size))) {
	    std::stringstream msg;
	    msg << "replace size mismatch: "
		<< mo->name << "[" << mo->size << "]"
		<< " vs " << obj->name << "[" << obj->numBytes << "]"
		<< " in test\n";

            terminateStateOnError(state, msg.str(), User);
            return;
          } else {
            std::vector<unsigned char> &values = si.assignment.bindings[array];
            values.insert(values.begin(), obj->bytes, 
                          obj->bytes + std::min(obj->numBytes, mo->size));
            if (ZeroSeedExtension) {
              for (unsigned i=obj->numBytes; i<mo->size; ++i)
                values.push_back('\0');
            }
          }
        }
      }
    }
  } else {
    newOs = bindObjectInState(state, mo, false);
    if (replayPosition >= replayKTest->numObjects) {
      terminateStateOnError(state, "replay count mismatch", User);
      return;
    } else {
      KTestObject *obj = &replayKTest->objects[replayPosition++];
      if (obj->numBytes != mo->size) {
        terminateStateOnError(state, "replay size mismatch", User);
        return;
      } else {
        for (unsigned i=0; i<mo->size; i++) {
          newOs->write8(i, obj->bytes[i]);
        }
      }
    }
  }
  if (PruneStates)
    state.memoryState.registerWrite(*mo, *newOs);
}



/***/

void Executor::runFunctionAsMain(Function *f,
				 int argc,
				 char **argv,
				 char **envp) {
  std::vector<ref<Expr> > arguments;

  // force deterministic initialization of memory objects
  srand(1);
  srandom(1);

  // We have to create the initial state as one of the first actions since otherwise
  // we cannot correctly initialize / allocate the needed memory regions
  auto *state = new ExecutionState(kmodule->functionMap[f]);

  // By default the state creates and executes the main thread
  state->thread().threadHeapAlloc = memory->createThreadHeapAllocator(state->tid());
  state->thread().threadStackAlloc = memory->createThreadStackAllocator(state->tid());

  MemoryObject *argvMO = nullptr;

  // In order to make uclibc happy and be closer to what the system is
  // doing we lay out the environments at the end of the argv array
  // (both are terminated by a null). There is also a final terminating
  // null that uclibc seems to expect, possibly the ELF header?

  int envc;
  for (envc=0; envp[envc]; ++envc) ;

  unsigned NumPtrBytes = Context::get().getPointerWidth() / 8;
  KFunction *kf = kmodule->functionMap[f];
  assert(kf);
  Function::arg_iterator ai = f->arg_begin(), ae = f->arg_end();
  if (ai!=ae) {
    arguments.push_back(ConstantExpr::alloc(argc, Expr::Int32));
    if (++ai!=ae) {
      Instruction *first = &*(f->begin()->begin());
      argvMO =
          memory->allocateGlobal((argc + 1 + envc + 1 + 1) * NumPtrBytes,
                           /*allocSite=*/first, /*threadId=*/ state->tid(),/*alignment=*/8);

      if (!argvMO)
        klee_error("Could not allocate memory for function arguments");

      arguments.push_back(argvMO->getBaseExpr());

      if (++ai!=ae) {
        uint64_t envp_start = argvMO->address + (argc+1)*NumPtrBytes;
        arguments.push_back(Expr::createPointer(envp_start));

        if (++ai!=ae)
          klee_error("invalid main function (expect 0-3 arguments)");
      }
    }
  }

  if (DebugPrintCalls) {
    std::stringstream tmp;
    tmp << "[state: " << std::setw(6) << 0 << " thread: " << std::setw(2) << 0 << "] ";
    llvm::errs() << tmp.str() << f->getName() << "\n";
  }

  if (pathWriter) 
    state->pathOS = pathWriter->open();
  if (symPathWriter) 
    state->symPathOS = symPathWriter->open();


  if (statsTracker) {
    statsTracker->framePushed(&state->stackFrame(), 0);
  }

  assert(arguments.size() == f->arg_size() && "wrong number of arguments");
  for (unsigned i = 0, e = f->arg_size(); i != e; ++i)
    bindArgument(kf, i, *state, arguments[i]);

  if (argvMO) {
    ObjectState *argvOS = bindObjectInState(*state, argvMO, false);

    for (int i=0; i<argc+1+envc+1+1; i++) {
      if (i==argc || i>=argc+1+envc) {
        // Write NULL pointer
        argvOS->write(i * NumPtrBytes, Expr::createPointer(0));
      } else {
        char *s = i<argc ? argv[i] : envp[i-(argc+1)];
        int j, len = strlen(s);

        MemoryObject *arg =
            memory->allocateGlobal(len + 1,
                             /*allocSite=*/state->pc()->inst,
                             /*tid=*/state->tid(),
                             /*alignment=*/8);
        if (!arg)
          klee_error("Could not allocate memory for function arguments");
        ObjectState *os = bindObjectInState(*state, arg, false);
        for (j=0; j<len+1; j++)
          os->write8(j, s[j]);

        // Write pointer to newly allocated and initialised argv/envp c-string
        argvOS->write(i * NumPtrBytes, arg->getBaseExpr());
      }
    }
    if (PruneStates) {
      state->memoryState.registerWrite(*argvMO, *argvOS);
    }
  }
  
  initializeGlobals(*state);

  processTree = std::make_unique<PTree>(state);

  auto rootNode = std::make_unique<por::node>();
  state->porNode = rootNode.get();

  // register thread_init event for main thread at last possible moment
  // to ensure that all data structures are properly set up
  porEventManager.registerThreadInit(*state, state->tid());

  auto unfolding = rootNode->configuration().unfolding();

  run(*state);
  processTree = nullptr;

  rootNode.reset();

  // hack to clear memory objects
  delete memory;
  memory = new MemoryManager(NULL);

  if (statsTracker)
    statsTracker->done();

  // FIXME: find a more appropriate place for this
  if (DebugPrintPorStats) {
    unfolding->print_statistics();
    llvm::outs() << "\n";
    llvm::outs() << "KLEE: done: instructions during catch-up = " << stats::catchUpInstructions << "\n";
    llvm::outs() << "KLEE: done: standby states = " << stats::standbyStates << "\n";
    llvm::outs() << "KLEE: done: maximal configurations = " << stats::maxConfigurations << "\n";
  }
}

unsigned Executor::getPathStreamID(const ExecutionState &state) {
  assert(pathWriter);
  return state.pathOS.getID();
}

unsigned Executor::getSymbolicPathStreamID(const ExecutionState &state) {
  assert(symPathWriter);
  return state.symPathOS.getID();
}

void Executor::getConstraintLog(const ExecutionState &state, std::string &res,
                                Interpreter::LogType logFormat) {

  switch (logFormat) {
  case STP: {
    Query query(state.constraints, ConstantExpr::alloc(0, Expr::Bool));
    char *log = solver->getConstraintLog(query);
    res = std::string(log);
    free(log);
  } break;

  case KQUERY: {
    std::string Str;
    llvm::raw_string_ostream info(Str);
    ExprPPrinter::printConstraints(info, state.constraints);
    res = info.str();
  } break;

  case SMTLIB2: {
    std::string Str;
    llvm::raw_string_ostream info(Str);
    ExprSMTLIBPrinter printer;
    printer.setOutput(info);
    Query query(state.constraints, ConstantExpr::alloc(0, Expr::Bool));
    printer.setQuery(query);
    printer.generateOutput();
    res = info.str();
  } break;

  default:
    klee_warning("Executor::getConstraintLog() : Log format not supported!");
  }
}

bool Executor::getSymbolicSolution(const ExecutionState &state,
                                   std::vector< 
                                   std::pair<std::string,
                                   std::vector<unsigned char> > >
                                   &res) {
  solver->setTimeout(coreSolverTimeout);

  ExecutionState tmp(state);
  updateForkJSON(state, tmp, tmp);

  // Go through each byte in every test case and attempt to restrict
  // it to the constraints contained in cexPreferences.  (Note:
  // usually this means trying to make it an ASCII character (0-127)
  // and therefore human readable. It is also possible to customize
  // the preferred constraints.  See test/Features/PreferCex.c for
  // an example) While this process can be very expensive, it can
  // also make understanding individual test cases much easier.
  for (unsigned i = 0; i != state.symbolics.size(); ++i) {
    const MemoryObject *mo = state.symbolics[i].first;
    std::vector< ref<Expr> >::const_iterator pi = 
      mo->cexPreferences.begin(), pie = mo->cexPreferences.end();
    for (; pi != pie; ++pi) {
      bool mustBeTrue;
      // Attempt to bound byte to constraints held in cexPreferences
      bool success = solver->mustBeTrue(tmp, Expr::createIsZero(*pi), 
					mustBeTrue);
      // If it isn't possible to constrain this particular byte in the desired
      // way (normally this would mean that the byte can't be constrained to
      // be between 0 and 127 without making the entire constraint list UNSAT)
      // then just continue on to the next byte.
      if (!success) break;
      // If the particular constraint operated on in this iteration through
      // the loop isn't implied then add it to the list of constraints.
      if (!mustBeTrue) addConstraint(tmp, *pi);
    }
    if (pi!=pie) break;
  }

  std::vector< std::vector<unsigned char> > values;
  std::vector<const Array*> objects;
  for (unsigned i = 0; i != state.symbolics.size(); ++i)
    objects.push_back(state.symbolics[i].second);
  bool success = solver->getInitialValues(tmp, objects, values);
  solver->setTimeout(time::Span());
  if (!success) {
    klee_warning("unable to compute initial values (invalid constraints?)!");
    ExprPPrinter::printQuery(llvm::errs(), state.constraints,
                             ConstantExpr::alloc(0, Expr::Bool));
    return false;
  }
  
  for (unsigned i = 0; i != state.symbolics.size(); ++i)
    res.push_back(std::make_pair(state.symbolics[i].first->name, values[i]));
  return true;
}

void Executor::getCoveredLines(const ExecutionState &state,
                               std::map<const std::string*, std::set<unsigned> > &res) {
  res = state.coveredLines;
}

void Executor::doImpliedValueConcretization(ExecutionState &state,
                                            ref<Expr> e,
                                            ref<ConstantExpr> value) {
  abort(); // FIXME: Broken until we sort out how to do the write back.

  if (DebugCheckForImpliedValues)
    ImpliedValue::checkForImpliedValues(solver->solver, e, value);

  ImpliedValueList results;
  ImpliedValue::getImpliedValues(e, value, results);
  for (ImpliedValueList::iterator it = results.begin(), ie = results.end();
       it != ie; ++it) {
    ReadExpr *re = it->first.get();
    
    if (ConstantExpr *CE = dyn_cast<ConstantExpr>(re->index)) {
      // FIXME: This is the sole remaining usage of the Array object
      // variable. Kill me.
      const MemoryObject *mo = 0; //re->updates.root->object;
      const ObjectState *os = state.addressSpace.findObject(mo);

      if (!os) {
        // object has been free'd, no need to concretize (although as
        // in other cases we would like to concretize the outstanding
        // reads, but we have no facility for that yet)
      } else {
        assert(!os->readOnly && 
               "not possible? read only object with static read?");
        ObjectState *wos = state.addressSpace.getWriteable(mo, os);
        wos->write(CE, it->second);
      }
    }
  }
}

Expr::Width Executor::getWidthForLLVMType(llvm::Type *type) const {
  return kmodule->targetData->getTypeSizeInBits(type);
}

size_t Executor::getAllocationAlignment(const llvm::Value *allocSite) const {
  // FIXME: 8 was the previous default. We shouldn't hard code this
  // and should fetch the default from elsewhere.
  const size_t forcedAlignment = 8;
  size_t alignment = 0;
  llvm::Type *type = NULL;
  std::string allocationSiteName(allocSite->getName().str());
  if (const GlobalValue *GV = dyn_cast<GlobalValue>(allocSite)) {
    alignment = GV->getAlignment();
    if (const GlobalVariable *globalVar = dyn_cast<GlobalVariable>(GV)) {
      // All GlobalVariables's have pointer type
      llvm::PointerType *ptrType =
          dyn_cast<llvm::PointerType>(globalVar->getType());
      assert(ptrType && "globalVar's type is not a pointer");
      type = ptrType->getElementType();
    } else {
      type = GV->getType();
    }
  } else if (const AllocaInst *AI = dyn_cast<AllocaInst>(allocSite)) {
    alignment = AI->getAlignment();
    type = AI->getAllocatedType();
  } else if (isa<InvokeInst>(allocSite) || isa<CallInst>(allocSite)) {
    // FIXME: Model the semantics of the call to use the right alignment
    llvm::Value *allocSiteNonConst = const_cast<llvm::Value *>(allocSite);
    const CallSite cs = (isa<InvokeInst>(allocSiteNonConst)
                             ? CallSite(cast<InvokeInst>(allocSiteNonConst))
                             : CallSite(cast<CallInst>(allocSiteNonConst)));
    llvm::Function *fn =
        klee::getDirectCallTarget(cs, /*moduleIsFullyLinked=*/true);
    if (fn)
      allocationSiteName = fn->getName().str();

    klee_warning_once(fn != NULL ? fn : allocSite,
                      "Alignment of memory from call \"%s\" is not "
                      "modelled. Using alignment of %zu.",
                      allocationSiteName.c_str(), forcedAlignment);
    alignment = forcedAlignment;
  } else {
    llvm_unreachable("Unhandled allocation site");
  }

  if (alignment == 0) {
    assert(type != NULL);
    // No specified alignment. Get the alignment for the type.
    if (type->isSized()) {
      alignment = kmodule->targetData->getPrefTypeAlignment(type);
    } else {
      klee_warning_once(allocSite, "Cannot determine memory alignment for "
                                   "\"%s\". Using alignment of %zu.",
                        allocationSiteName.c_str(), forcedAlignment);
      alignment = forcedAlignment;
    }
  }

  // Currently we require alignment be a power of 2
  if (!bits64::isPowerOfTwo(alignment)) {
    klee_warning_once(allocSite, "Alignment of %zu requested for %s but this "
                                 "not supported. Using alignment of %zu",
                      alignment, allocSite->getName().str().c_str(),
                      forcedAlignment);
    alignment = forcedAlignment;
  }
  assert(bits64::isPowerOfTwo(alignment) &&
         "Returned alignment must be a power of two");
  return alignment;
}

void Executor::prepareForEarlyExit() {
  if (statsTracker) {
    // Make sure stats get flushed out
    statsTracker->done();
  }
}

ThreadId Executor::createThread(ExecutionState &state,
                                KFunction *startRoutine,
                                ref<Expr> runtimeStructPtr) {

  Thread &thread = state.createThread(startRoutine, runtimeStructPtr);
  StackFrame *threadStartFrame = &thread.stack.back();

  threadStartFrame->locals[startRoutine->getArgRegister(0)].value = runtimeStructPtr;

  // If we create a thread, then we also have to create the memory region and the TLS objects
  thread.threadHeapAlloc = memory->createThreadHeapAllocator(thread.getThreadId());
  thread.threadStackAlloc = memory->createThreadStackAllocator(thread.getThreadId());

  // Errno is one of the tls objects
  std::uint64_t alignment = alignof(errno);
  std::uint64_t size = sizeof(*getErrnoLocation(state));

  MemoryObject* thErrno = memory->allocate(size, true, nullptr, thread, 0, alignment);
  if (thErrno == nullptr) {
    klee_error("Could not allocate memory for thread local objects");
  }

  thread.errnoMo = thErrno;

  // And initialize the errno
  ObjectState *errNoOs = bindObjectInState(state, thErrno, false);
  errNoOs->initializeToRandom();
  if (PruneStates) {
    state.memoryState.registerWrite(*thErrno, *errNoOs);
  }

  // Now all the other TLS objects have to be initialized (e.g. the globals)
  // once all objects are allocated, do the actual initialization
  auto m = kmodule->module.get();
  std::vector<ObjectState *> constantObjects;
  for (auto i = m->global_begin(), e = m->global_end(); i != e; ++i) {
    const GlobalVariable *v = &*i;

    if (i->hasInitializer() && i->isThreadLocal()) {
      auto mo = memory->lookupGlobalMemoryObject(v, thread.getThreadId());

      ObjectState *os = bindObjectInState(state, mo, false);
      initializeGlobalObject(state, os, i->getInitializer(), 0, thread.getThreadId());

      if (i->isConstant()) {
        constantObjects.emplace_back(os);
      }
    }
  }

  // initialize constant memory that is potentially used with external calls
  if (!constantObjects.empty()) {
    // initialize the actual memory with constant values
    state.addressSpace.copyOutConcretes();

    // mark constant objects as read-only
    for (auto obj : constantObjects)
      obj->setReadOnly(true);
  }

  if (statsTracker)
    statsTracker->framePushed(threadStartFrame, nullptr);

  porEventManager.registerThreadCreate(state, thread.getThreadId());
  porEventManager.registerThreadInit(state, thread.getThreadId());

  return thread.getThreadId();
}

void Executor::exitCurrentThread(ExecutionState &state, bool callToExit) {
  // needs to come before thread_exit event
  if (state.isOnMainThread() && state.hasUnregisteredDecisions()) {
    static std::vector<ExecutionState *> emptyVec;
    porEventManager.registerLocal(state, emptyVec, false);
  }

  state.exitThread(callToExit);

  auto m = kmodule->module.get();
  for (auto i = m->global_begin(), e = m->global_end(); i != e; ++i) {
    const GlobalVariable *v = &*i;

    if (v->isThreadLocal()) {
      auto mo = memory->lookupGlobalMemoryObject(v, state.tid());

      processMemoryAccess(state, mo, nullptr, 0, MemoryOperation::Type::FREE);

      if (PruneStates) {
        auto os = state.addressSpace.findObject(mo);
        state.memoryState.unregisterWrite(*mo, *os);
      }

      state.addressSpace.unbindObject(mo);
    }
  }
}

bool
Executor::processMemoryAccess(ExecutionState &state, const MemoryObject *mo, const ref<Expr> &offset,
                              std::size_t numBytes, MemoryOperation::Type type) {
  if (!EnableDataRaceDetection) {
    // These accesses are always safe and do not need to be tracked
    return true;
  }

  MemoryOperation operation{};
  operation.object = mo;
  operation.offset = offset;
  operation.numBytes = numBytes;
  operation.tid = state.tid();
  operation.instruction = state.prevPc();
  operation.type = type;

  StateBoundTimingSolver solv(state, *solver, coreSolverTimeout);

  auto result = state.raceDetection.isDataRace(*state.porNode, solv, operation);
  if (!result.has_value()) {
    klee_warning("Failure at determining whether an accesses races - assuming safe access");
    state.raceDetection.trackAccess(*state.porNode, operation);
    return true;
  }

  if (result->isRace) {
    // So two important cases: always racing or only racing with specific symbolic values
    if (result->canBeSafe && false) { // FIXME: incompleteness; handle catch-up problem!
      auto statePair = fork(state, result->conditionToBeSafe, true);

      auto safeState = statePair.first;
      auto unsafeState = statePair.second;

      // So whenever we are in a catch-up mode, then it actually can happen, that we get different results
      // -> the constraints are only added after the fork call
      // FIXME: either assert here that we are actually in a catch-up or add the constraints earlier
      //        so that the data race detection is not fooled
      // assert(safeState != nullptr && unsafeState != nullptr && "Solver returned different results the second time");

      if (safeState == nullptr) {
        assert(unsafeState != nullptr);
        assert(unsafeState == &state);

        terminateStateOnUnsafeMemAccess(state, mo, result->racingThread, result->racingInstruction);
        return false;
      } else if (unsafeState == nullptr) {
        assert(safeState != nullptr);
        assert(safeState == &state);
        // So a constraint was added during fork that made the race only safe -> fake this correctly
        state.raceDetection.trackAccess(*state.porNode, operation);

        // No need to add the safe constraints as it was added during fork
        // TODO: maybe we actuall want to add it? Just to be sure?
        return true;
      } else {
        terminateStateOnUnsafeMemAccess(*unsafeState, mo, result->racingThread, result->racingInstruction);

        safeState->raceDetection.trackAccess(*state.porNode, operation);

        return safeState == &state;
      }
    } else {
      // Now the racing part
      terminateStateOnUnsafeMemAccess(state, mo, result->racingThread, result->racingInstruction);
      return false;
    }
  } else {
    if (result->hasNewConstraints) {
      addConstraint(state, result->newConstraints);
    }

    state.raceDetection.trackAccess(*state.porNode, operation);
    return true;
  }
}

void
Executor::terminateStateOnUnsafeMemAccess(ExecutionState &state, const MemoryObject *mo, const ThreadId &racingThread,
                                          KInstruction *racingInstruction) {
  std::string TmpStr;
  llvm::raw_string_ostream os(TmpStr);
  os << "Unsafe access to memory from multiple threads\nAffected memory: ";

  std::string memInfo;
  mo->getAllocInfo(memInfo);
  os << memInfo << "\n";

  os << "--- Executed\n";
  os << state.tid() << " races with " << racingThread;

  const InstructionInfo &ii = *racingInstruction->info;
  if (!ii.file.empty()) {
    os << " instruction in: " << ii.file << ":" << ii.line << "\n";
  } else {
    os << " location of instruction unknown\n";
  }

  os << "--- Operations\n";

  os << racingThread
    << " -> " << *racingInstruction->inst
    << " (assembly.ll:" << ii.assemblyLine << ")"
    << "\n";

  os << state.tid()
    << " -> " << *state.prevPc()->inst
    << " (assembly.ll:" << state.prevPc()->info->assemblyLine << ")"
    << "\n";

  terminateStateOnError(state, "thread unsafe memory access",
                        UnsafeMemoryAccess, nullptr, os.str());
}

void Executor::terminateStateOnDeadlock(ExecutionState &state) {
  std::string TmpStr;
  llvm::raw_string_ostream os(TmpStr);
  os << "Deadlock in scheduling with ";
  state.dumpSchedulingInfo(os);
  os << "Traces:\n";
  state.dumpAllThreadStacks(os);

  terminateStateOnError(state, "all non-exited threads are waiting on resources",
                        Deadlock, nullptr, os.str());
}

void Executor::registerFork(ExecutionState &state, ExecutionState* fork) {
  processTree->attach(state.ptreeNode, fork, &state);

  if (pathWriter) {
    fork->pathOS = pathWriter->open(state.pathOS);
  }

  if (symPathWriter) {
    fork->symPathOS = symPathWriter->open(state.symPathOS);
  }
}

void Executor::scheduleThreads(ExecutionState &state) {
  std::set<ThreadId> runnable = state.runnableThreads();

  assert(state.porNode);
  auto cfg = state.porNode->configuration();

  ThreadId tid;

  while (true) {
    while (state.needsCatchUp()) {
      auto peekTid = state.peekCatchUp()->tid();
      auto peekThread = state.getThreadById(peekTid);

      assert(peekThread.has_value());
      tid = peekTid;

      Thread& nextThread = peekThread.value().get();
      assert(nextThread.state != ThreadState::Cutoff);
      if (nextThread.state == ThreadState::Waiting && nextThread.isRunnable(cfg)) {
        scheduleNextThread(state, tid);

        runnable = state.runnableThreads();
        continue;
      }

      break;
    }

    if (!state.needsCatchUp()) {
      auto res = selectStateForScheduling(state, runnable);
      if (!res) {
        return;
      }
      tid = res.value();
    }

    state.needsThreadScheduling = false;
    scheduleNextThread(state, tid);

    if (state.threadState() == ThreadState::Runnable) {
      return;
    }

    runnable = state.runnableThreads();
  }
}

std::optional<ThreadId> Executor::selectStateForScheduling(ExecutionState &state, std::set<ThreadId> &runnable) {
  bool disabledThread = false;
  bool wasEmpty = runnable.empty();

  if (!state.needsCatchUp() && !state.porNode->D().empty()) {
    const por::configuration &C = state.porNode->configuration();
    std::map<ThreadId, std::deque<const por::event::event *>> D;
    for(auto &event : state.porNode->D()) {
      auto it = D.find(event->tid());
      if(it != D.end()) {
        it->second.push_back(event);
      } else {
        D.emplace(event->tid(), std::deque<const por::event::event *>{event});
      }
    }

    for (auto &[tid, events] : D) {
      if (!C.thread_heads().count(tid)) {
        continue; // go to next thread
      }
      for (auto &d : events) {
        if (d->depth() <= C.thread_heads().at(tid)->depth()) {
          // d is justified, no need to exclude it anymore
          continue; // go to next event
        }
        // d is excluded
        if (d->is_enabled(C)) {
          bool isJustified = false;
          if (d->lid()) {
            if (C.lock_heads().count(d->lid()) == 0 && d->lock_predecessor() != nullptr) {
              isJustified = true;
            } else if(C.lock_heads().count(d->lid())) {
              if (d->lock_predecessor() != C.lock_heads().at(d->lid())) {
                isJustified = true;
              }
            }
          }

          if (!isJustified) {
            if (runnable.erase(d->tid()) > 0) {
              disabledThread = true;
            }
            break; // go to next thread
          }
        }
      }
    }
  }

  // Another point of we cannot schedule any other thread
  if (runnable.empty()) {
    if (disabledThread && !wasEmpty) {
      klee_warning("Disabled all threads because of porNode->D(). Terminating State.");
      terminateState(state);
      return {};
    }

    bool allExited = true;
    bool cutoffPresent = false;

    for (auto& threadIt : state.threads) {
      if (threadIt.second.state != ThreadState::Exited && threadIt.second.state != ThreadState::Cutoff) {
        allExited = false;
      } else if (threadIt.second.state == ThreadState::Cutoff) {
        cutoffPresent = true;
      }
    }

    if (allExited || cutoffPresent || state.calledExit) {
      terminateStateOnExit(state);
    } else {
      terminateStateOnDeadlock(state);
    }

    return {};
  }

  // pick thread according to policy by default
  ThreadId tid;
  switch (ThreadScheduling) {
    case ThreadSchedulingPolicy::First:
      tid = *runnable.begin();
      break;
    case ThreadSchedulingPolicy::Last:
      tid = *std::prev(runnable.end());
      break;
    case ThreadSchedulingPolicy::Random:
      tid = *std::next(runnable.begin(), theRNG.getInt32() % runnable.size());
      break;
    case ThreadSchedulingPolicy::RoundRobin: {
      tid = *std::next(runnable.begin(), state.porNode->configuration().size() % runnable.size());
      break;
    }
  }

  return tid;
}

void Executor::scheduleNextThread(ExecutionState &state, const ThreadId &tid) {
  auto res = state.getThreadById(tid);
  assert(res.has_value());
  Thread &thread = res->get();
  auto previous = state.runThread(thread);
  if (!std::holds_alternative<Thread::wait_none_t>(previous)) {
    // NOTE: event registration has to come last for consistent standby state
    std::visit([this,&state](auto&& w) {
      using T = std::decay_t<decltype(w)>;
      if constexpr (std::is_same_v<T, Thread::wait_lock_t>) {
        porEventManager.registerLockAcquire(state, w.lock);
      } else if constexpr (std::is_same_v<T, Thread::wait_cv_2_t>) {
        porEventManager.registerCondVarWait2(state, w.cond, w.lock);
      } else if constexpr (std::is_same_v<T, Thread::wait_join_t>) {
        porEventManager.registerThreadJoin(state, w.thread);
      } else {
        assert(0 && "thread cannot be woken up!");
      }
    }, previous);
  }
}

/// Returns the errno location in memory
int *Executor::getErrnoLocation(const ExecutionState &state) const {
#if !defined(__APPLE__) && !defined(__FreeBSD__)
  /* From /usr/include/errno.h: it [errno] is a per-thread variable. */
  return __errno_location();
#else
  return __error();
#endif
}


void Executor::dumpPTree() {
  if (!::dumpPTree) return;

  char name[32];
  snprintf(name, sizeof(name),"ptree%08d.dot", (int) stats::instructions);
  auto os = interpreterHandler->openOutputFile(name);
  if (os) {
    processTree->dump(*os);
  }

  ::dumpPTree = 0;
}

void Executor::dumpStates() {
  if (!::dumpStates) return;

  auto os = interpreterHandler->openOutputFile("states.txt");

  if (os) {
    for (ExecutionState *es : states) {
      *os << "(" << es << ",";
      *os << "[";
      if (es->threadState() == ThreadState::Exited) {
        // FIXME: find more appropriate way to handle this (instead of skipping state entirely)
        continue;
      }
      auto next = es->stack().begin();
      ++next;
      for (auto sfIt = es->stack().begin(), sf_ie = es->stack().end();
            sfIt != sf_ie; ++sfIt) {
        *os << "('" << sfIt->kf->function->getName().str() << "',";
        if (next == es->stack().end()) {
          *os << es->prevPc()->info->line << "), ";
        } else {
          *os << next->caller->info->line << "), ";
          ++next;
        }
      }
      *os << "], ";

      const StackFrame &sf = es->stackFrame();
      uint64_t md2u = computeMinDistToUncovered(es->pc(),
                                                sf.minDistToUncoveredOnReturn);
      uint64_t icnt = theStatisticManager->getIndexedValue(stats::instructions,
                                                           es->pc()->info->id);
      uint64_t cpicnt = sf.callPathNode->statistics.getValue(stats::instructions);

      *os << "{";
      *os << "'depth' : " << es->depth << ", ";
      *os << "'queryCost' : " << es->queryCost << ", ";
      *os << "'coveredNew' : " << es->coveredNew << ", ";
      *os << "'instsSinceCovNew' : " << es->instsSinceCovNew << ", ";
      *os << "'md2u' : " << md2u << ", ";
      *os << "'icnt' : " << icnt << ", ";
      *os << "'CPicnt' : " << cpicnt << ", ";
      *os << "}";
      *os << ")\n";
    }
  }

  ::dumpStates = 0;
}

///

Interpreter *Interpreter::create(LLVMContext &ctx, const InterpreterOptions &opts,
                                 InterpreterHandler *ih) {
  return new Executor(ctx, opts, ih);
}
