//===-- LiveRegister.cpp --------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Passes.h"

#if LLVM_VERSION_CODE < LLVM_VERSION(3, 5)
#include "llvm/Support/CFG.h"
#include "llvm/Support/InstIterator.h"
#else
#include "llvm/IR/CFG.h"
#include "llvm/IR/InstIterator.h"
#endif

#include "llvm/ADT/SetOperations.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/User.h"

using namespace llvm;

namespace {

template<typename T>
void printValuesAsSet(raw_ostream &os, T &set) {
  auto values = std::vector<const Value *>(set.begin(), set.end());
  std::sort(values.begin(), values.end(), [](const Value *a, const Value *b) {
    if (!a->hasName()) return false;
    if (b->hasName())
      return a->getName() < b->getName();
    return true;
  });
  os << "{";
  bool first = true;
  for (const Value *value : values) {
    os << (first ? "" : ", ");
    if (value->hasName())
      os << "%" << value->getName();
    else
      os << "unnamed";
    first = false;
  }
  os << "}\n";
}

}

namespace klee {

bool LiveRegisterPass::runOnFunction(Function &F) {
  this->F = &F;

  // Add nop instruction before the first instruction of each basic block
  // to get live registers at the beginning of each basic block (as our analysis
  // propagates backwards, we would otherwise only know which registers are live
  // after the execution of the first instruction of each basic block).
  // Introduced instructions are removed again at the end of this pass.
  for (auto it = F.begin(), ie = F.end(); it != ie; ++it) {
    insertNopInstruction(*it);

  }

  initializeWorklist(F);
  executeWorklistAlgorithm();
  propagatePhiUseToLiveSet(F);

  computeBasicBlockInfo(F);

  // remove nop instructions that were added in the initialization phase
  for (auto it = F.begin(), ie = F.end(); it != ie; ++it) {
    BasicBlock &bb = *it;
    Instruction &nop = *bb.begin();
    nop.eraseFromParent();
  }

  return false;
}

void LiveRegisterPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
}

void LiveRegisterPass::initializeWorklist(const Function &F) {
  // clean up from previous runs
  worklist.clear();
  instructions.clear();

  // generate gen and kill sets for each instruction
  generateInstructionInfo(F);

  for (auto it = inst_begin(F), ie = inst_end(F); it != ie; ++it) {
    addPredecessors(worklist, &*it);
  }
}

void LiveRegisterPass::executeWorklistAlgorithm() {
  while (!worklist.empty()) {
    const Instruction *i = worklist.back().first;
    const Instruction *pred = worklist.back().second;
    worklist.pop_back();

    InstructionInfo &iII = instructions[i];
    InstructionInfo &predII = instructions[pred];

    valueset_t liveUpdated = transition(i, iII.live);
    if (set_union(predII.live, liveUpdated)) {
      // predII.live has changed
      addPredecessors(worklist, pred);
    }
  }
}

void LiveRegisterPass::propagatePhiUseToLiveSet(const Function &F) {
  for (auto it = F.begin(), ie = F.end(); it != ie; ++it) {
    const BasicBlock &bb = *it;
    const Instruction *term = bb.getTerminator();
    valueset_t &termLive = instructions[term].live;
    valueset_t &termGen = instructions[term].gen;

    // If a register is only used by PHI nodes in all following blocks, it is
    // included in the gen set of the terminator instruction but was not yet
    // propagated into its live set. Here, we simply insert all values that are
    // used by PHI nodes into the terminator instruction's live set.
    for (const Value *value : termGen) {
#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 5)
      for (auto i = value->user_begin(), e = value->user_end(); i != e; ++i) {
#else
      for (auto i = value->use_begin(), e = value->use_end(); i != e; ++i) {
#endif
        if (const Instruction *inst = dyn_cast<Instruction>(*i)) {
          if (inst->getOpcode() == Instruction::PHI) {
            // found usage (of value in gen set) by a PHI node
            termLive.insert(value);

            // we do only need to add each value once to the live set
            break;
          }
        }
      }
    }
  }
}

const Instruction *LiveRegisterPass::getLastPHIInstruction(const BasicBlock &BB) {
  auto it = BB.begin();
  auto ie = BB.end();
  bool firstIsPHI = (it->getOpcode() == Instruction::PHI);
  if (std::next(it) == ie) {
    // only one instruction in BB
    assert(!firstIsPHI && "PHINode is not a terminator instruction");
    return &*it;
  }

  bool secondIsPHI = (std::next(it)->getOpcode() == Instruction::PHI);
  if (!firstIsPHI && secondIsPHI)
    ++it; // skip NOP instruction (only if PHI nodes are available)

  const Instruction *inst = &*it;
  do {
    inst = &*it;
  } while (it != ie && (++it)->getOpcode() == Instruction::PHI);

  return inst;
}

void LiveRegisterPass::computeBasicBlockInfo(const Function &F) {
  for (auto it = F.begin(), ie = F.end(); it != ie; ++it) {
    const BasicBlock &bb = *it;
    BasicBlockInfo &bbInfo = basicBlocks[&bb];

    // "lastPHI": last PHI node if any, otherwise NOP instruction
    bbInfo.lastPHI = getLastPHIInstruction(bb);

    // "phiLive": registers that are live after the last PHI node or NOP
    // In other words: before first "real" instruction.
    bbInfo.phiLive = &instructions[bbInfo.lastPHI].live;

    // "termLive": registers that are live at the end of the BB
    bbInfo.termLive = &instructions[bb.getTerminator()].live;

    // "consumed": registers that are not read in any of BB's successors
    // this can be values from previous BB but also ones written in PHI nodes
    bbInfo.consumed = set_difference(*bbInfo.phiLive, *bbInfo.termLive);

    for (auto it = pred_begin(&bb), ie = pred_end(&bb); it != ie; ++it) {
      const BasicBlock &pred = **it;
      BasicBlockInfo &predInfo = basicBlocks[&pred];
      const Instruction *predTerm = pred.getTerminator();

      // "killed": registers that are not used in `bb` succeeding `pred` after
      // evaluation of PHI nodes, but may be live in other BBs succeeding `pred`
      predInfo.killed[&bb] = set_difference(instructions[predTerm].live,
                                            *bbInfo.phiLive);
    }
  }
}

void LiveRegisterPass::generateInstructionInfo(const Function &F) {
  basicBlocks.reserve(F.size());
  size_t numInstructions = 0;
  // iterate over all basic blocks
  for (auto it = F.begin(), ie = F.end(); it != ie; ++it) {
    const BasicBlock &bb = *it;
    basicBlocks[&bb] = {};
    numInstructions += bb.size();
    instructions.reserve(numInstructions);
    // iterate over all instructions within a basic block
    for (auto it = bb.begin(), ie = bb.end(); it != ie; ++it) {
      const Instruction *i = &*it;
      instructions[i] = {};
    }
  }

  // In the following loop, we assume that each instruction within the function
  // has a corresponding InstructionInfo entry, which is why we need to iterate
  // over all instructions twice.

  // iterate over all basic blocks
  for (auto it = F.begin(), ie = F.end(); it != ie; ++it) {
    const BasicBlock &bb = *it;
    const Instruction *previ = &*bb.begin();
    // iterate over all instructions within a basic block
    for (auto it = bb.begin(), ie = bb.end(); it != ie; ++it) {
      const Instruction *i = &*it;
      InstructionInfo &ii = instructions[i];

      // generate predecessorEdges
      if (it == bb.begin()) {
        // first instruction in basic block: insert terminator instructions of
        // all preceding basic blocks as predecessors
        for (auto it = pred_begin(&bb), ie = pred_end(&bb); it != ie; ++it) {
          const BasicBlock *pred = *it;
          if (const Instruction *predTerm = pred->getTerminator()) {
            ii.predecessorEdges.emplace_back(std::make_pair(i, predTerm));
          }
        }
      } else {
        // all further instructions: insert previous instruction as predecessor
        ii.predecessorEdges.emplace_back(std::make_pair(i, previ));
      }

      // parameters of functions are regarded as live and are thus ignored when
      // generating gen and kill sets

      // generate kill sets
      if (i->getNumUses() != 0) {
        ii.kill.insert(i);
      }

      // generate gen sets
      if (i->getOpcode() == Instruction::PHI) {
        // PHI nodes: attach operand to gen set of the incoming basic
        // block's terminator instruction because operands of PHI nodes
        // are not in general live at the start of the basic block that
        // contain the corresponding PHI node
        const PHINode *phi = cast<PHINode>(i);

        // iterate over all incoming basic blocks
        for (auto ib = phi->block_begin(), e = phi->block_end(); ib != e; ++ib)
        {
          const Value *value = phi->getIncomingValueForBlock(*ib);
          if (isa<Instruction>(value)) {
            const Instruction *incomingTerm = (*ib)->getTerminator();
            InstructionInfo &incomingII = instructions[incomingTerm];
            incomingII.gen.insert(value);
          }
        }
      } else if (i->getNumOperands() != 0) {
        // iterate over all operands (uses) of the current instruction
        for (auto use = i->op_begin(), ie = i->op_end(); use != ie; ++use) {
          if (const Instruction *op = dyn_cast<Instruction>(use->get())) {
            ii.gen.insert(op);
          }
        }
      }

      previ = i;
    }
  }
}

void LiveRegisterPass::print(raw_ostream &os, const Module *M) const {
  const Function &F = *this->F;
  for (auto it = F.begin(), ie = F.end(); it != ie; ++it) {
    const BasicBlock &bb = *it;
    const BasicBlockInfo &bbInfo = basicBlocks.at(&bb);
    const valueset_t &termLive = *bbInfo.termLive;
    const valueset_t &consumed = bbInfo.consumed;

    os << bb.getName() << ":\n";
    for (auto it = bb.begin(), ie = bb.end(); it != ie; ++it) {
      const Instruction &inst = *it;
      os << inst;
      if (inst.getOpcode() != Instruction::PHI || bbInfo.lastPHI == &inst) {
        os << " ; live = ";
        const valueset_t &instLive = instructions.at(&inst).live;
        printValuesAsSet(os, instLive);
      } else {
        os << "\n";
      }
    }
    os << "\n";

    os << "  consumed after PHI nodes (if any): ";
    printValuesAsSet(os, consumed);
    os << "  live after terminator instruction: ";
    printValuesAsSet(os, termLive);

    for (auto it = succ_begin(&bb), ie = succ_end(&bb); it != ie; ++it) {
      const BasicBlock &succ = **it;
      const valueset_t &killed = bbInfo.killed.at(&succ);

      os << "  killed on transition to " << succ.getName();
      os << " (after PHI node processing): ";
      printValuesAsSet(os, killed);
    }

    os << "\n";
  }
}

void LiveRegisterPass::addPredecessors(std::vector<edge_t> &worklist,
                                       const Instruction *i) {
  std::vector<edge_t> &predEdges = instructions[i].predecessorEdges;
  worklist.insert(worklist.end(), predEdges.begin(), predEdges.end());
}

LiveRegisterPass::valueset_t
LiveRegisterPass::transition(const Instruction *i, const valueset_t &set) {
  InstructionInfo &ii = instructions[i];
  valueset_t result = set;

  set_subtract(result, ii.kill);
  result.insert(ii.gen.begin(), ii.gen.end());
  return result;
}

void LiveRegisterPass::insertNopInstruction(BasicBlock &bb) {
  LLVMContext &ctx = bb.getContext();
  ConstantInt *zero = ConstantInt::get(ctx, APInt(1, 0, false));
  Instruction *nop = BinaryOperator::Create(Instruction::BinaryOps::Or, zero, zero);
  bb.getInstList().insert(bb.begin(), nop);
}


char LiveRegisterPass::ID = 0;
static RegisterPass<LiveRegisterPass> X("live-register", "Live Register Pass",
                                        false, false);

}
