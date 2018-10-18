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
#include "llvm/IR/Metadata.h"
#include "llvm/IR/User.h"

using namespace llvm;

namespace klee {

char LiveRegisterPass::ID = 0;

bool LiveRegisterPass::runOnFunction(Function &F) {
  // Add nop instruction before the first instruction of each basic block
  // to get live registers at the beginning of each basic block (as our analysis
  // propagates backwards, we would otherwise only know which registers are live
  // after the execution of the first instruction of each basic block).
  // Introduced instructions are removed again at the end of this pass.
  for (Function::iterator it = F.begin(), e = F.end(); it != e; ++it) {
    BasicBlock &bb = *it;
    LLVMContext &ctx = bb.getContext();
    bb.getInstList().insert(bb.begin(), createNopInstruction(ctx));
  }

  initializeWorklist(F);
  executeWorklistAlgorithm();
  propagatePhiUseToLiveSet(F);

  computeBasicBlockInfo(F);
  if (annotate)
    attachAnalysisResultAsMetadata(F);

  // remove nop instructions that were added in the initialization phase
  for (Function::iterator it = F.begin(), e = F.end(); it != e; ++it) {
    BasicBlock &bb = *it;
    Instruction &nop = *bb.begin();
    nop.eraseFromParent();
  }

  // was function modified by attaching metadata?
  return annotate;
}

void LiveRegisterPass::initializeWorklist(Function &F) {
  // clean up from previous runs
  worklist.clear();
  instructions.clear();

  // generate gen and kill sets for each instruction
  generateInstructionInfo(F);

  for (inst_iterator it = inst_begin(F), e = inst_end(F); it != e; ++it) {
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

void LiveRegisterPass::propagatePhiUseToLiveSet(Function &F) {
  for (Function::iterator it = F.begin(), e = F.end(); it != e; ++it) {
    BasicBlock &bb = *it;
    Instruction *term = bb.getTerminator();
    valueset_t &termLive = instructions[term].live;
    valueset_t &termGen = instructions[term].gen;

    // If a register is only used by PHI nodes in all following blocks, it is
    // included in the gen set of the terminator instruction but was not yet
    // propagated into its live set. Here, we simply insert all values that are
    // used by PHI nodes into the terminator instruction's live set.
    for (Value *value : termGen) {
#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 5)
      for (auto i = value->user_begin(), e = value->user_end(); i != e; ++i) {
#else
      for (auto i = value->use_begin(), e = value->use_end(); i != e; ++i) {
#endif
        if (Instruction *inst = dyn_cast<Instruction>(*i)) {
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

void LiveRegisterPass::computeBasicBlockInfo(Function &F) {
  for (Function::iterator it = F.begin(), e = F.end(); it != e; ++it) {
    BasicBlock &bb = *it;

    BasicBlockInfo &bbInfo = basicBlocks[&bb];

    // "firstLive": registers that are live at the start of BB
    // use live register set of nop instruction since instruction info only
    // reflects registers that are live *after* the execution of the associated
    // instruction
    bbInfo.firstLive = &instructions[&*bb.begin()].live;

    // "termLive": registers that are live at the end of the BB
    bbInfo.termLive = &instructions[bb.getTerminator()].live;

    // "consumed": registers that are not read in any of BB's successors
    bbInfo.consumed = set_difference(*bbInfo.firstLive, *bbInfo.termLive);
  }

  // in the following, we assume that consumed registers are already calculated
  // for all BBs

  for (Function::iterator it = F.begin(), e = F.end(); it != e; ++it) {
    BasicBlock &bb = *it;
    BasicBlockInfo &bbInfo = basicBlocks[&bb];

    std::vector<Value *> k;
    for (auto it = succ_begin(&bb), e = succ_end(&bb); it != e; ++it) {
      BasicBlock &succ = **it;
      BasicBlockInfo &succInfo = basicBlocks[&succ];

      // "killed": registers that are only used by specific succeeding BBs but
      // not by succ (the currently considered successor)
      auto &killed = bbInfo.killed[&succ];
      killed = set_difference(*bbInfo.termLive, *succInfo.firstLive);

      // exclude registers that are consumed during the succeeding basic block
      set_subtract(killed, succInfo.consumed);

      // exclude registers that are being written to by PHI nodes in
      // succeeding basic block
      for (auto it = std::next(succ.begin()), e = succ.end(); it != e; ++it) {
        const Instruction &i = *it;
        const Instruction *lastPHI = &i;
        if (i.getOpcode() == Instruction::PHI) {
          Value *phiValue = cast<Value>(const_cast<Instruction *>(&i));

          // last PHI node in current BasicBlock
          // Note: We cannot use getFirstNonPHI() here because of the NOP.
          while (lastPHI->getNextNode()->getOpcode() == Instruction::PHI) {
            lastPHI = lastPHI->getNextNode();
          }

          // values live after the evaluation of last PHI node
          valueset_t &phiLive = instructions[lastPHI].live;

          if (phiLive.count(phiValue) > 0) {
            // result of PHI node is live after evaluation of last PHI node
            killed.erase(phiValue);
            succInfo.termLive->insert(phiValue);
          }
        } else {
          // http://releases.llvm.org/3.4.2/docs/LangRef.html#phi-instruction
          // "There must be no non-phi instructions between the start of a
          //  basic block and the PHI instructions: i.e. PHI instructions must
          //  be first in a basic block."
          // Thus, we can abort as soon as we encounter a non-PHI instruction
          // (This is only valid here, because in this for-loop, we skip the
          // nop instruction inserted at the begining of each basic block)
          break;
        }
      }
    }
  }
}

void LiveRegisterPass::attachAnalysisResultAsMetadata(Function &F) {
  for (Function::iterator it = F.begin(), e = F.end(); it != e; ++it) {
    BasicBlock &bb = *it;
    LLVMContext &ctx = bb.getContext();
    BasicBlockInfo &bbInfo = basicBlocks[&bb];
    Instruction *term = bb.getTerminator();

    // attach to terminator instruction: live registers, i.e. registers that are
    // live at the end of the annotated basic block
    // Example:
    // (liveRegister1, liveRegister2, liveRegister3)
    valueset_t &termLive = *bbInfo.termLive;
#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 6)
    SmallVector<Metadata *, 10> liveVec;
    for (auto value : termLive) {
      liveVec.push_back(ValueAsMetadata::get(value));
    }
    term->setMetadata("liveregister.live", MDTuple::get(ctx, liveVec));
#else
    std::vector<Value *> liveVec(termLive.begin(), termLive.end());
    term->setMetadata("liveregister.live", MDNode::get(ctx, liveVec));
#endif

    // attach to terminator instruction: "consumed" registers, i.e. registers
    // that are read for the last time within the basic block
    // Example:
    // (consumedRegister1, consumedRegister2, consumedRegister3)
    valueset_t &consumed = bbInfo.consumed;
#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 6)
    SmallVector<Metadata *, 10> consumedVec;
    for (auto value : consumed) {
      consumedVec.push_back(ValueAsMetadata::get(value));
    }
    term->setMetadata("liveregister.consumed", MDTuple::get(ctx, consumedVec));
#else
    std::vector<Value *> consumedVec(consumed.begin(), consumed.end());
    term->setMetadata("liveregister.consumed", MDNode::get(ctx, consumedVec));
#endif

    // attach to terminator instruction: immediately killed registers for each
    // succeeding basic block, i.e. registers that are only used by specific
    // basic blocks but not the succeeding one
    // WARNING: we specifically exclude registers that are consumed during the
    //          execution of the succeding basic block from this set
    // Example:
    // (
    //   (succeedingBasicBlock1, (killedRegister1, killedRegister2)),
    //   (succeedingBasicBlock2, (killedRegister3, killedRegister3))
    // )
    // NOTE: From LLVM 3.6 on, we represent the succeeding basic block by its
    //       BlockAddress. This is due to the fact that the function
    //       ValueAsMetadata::get cannot handle BasicBlocks directly.
#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 6)
    SmallVector<Metadata *, 10> k;
#else
    std::vector<Value *> k;
#endif
    for (auto it = succ_begin(&bb), e = succ_end(&bb); it != e; ++it) {
      BasicBlock *succ = *it;

      valueset_t &killed = bbInfo.killed[succ];

#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 6)
      SmallVector<Metadata *, 10> killedVec;
      for (auto value : killed) {
        killedVec.push_back(ValueAsMetadata::get(value));
      }
      Constant *blockaddr = BlockAddress::get(succ);
      Metadata *tuple[2] = {ConstantAsMetadata::get(blockaddr),
                            MDTuple::get(ctx, killedVec)};
      k.push_back(MDTuple::get(ctx, tuple));
#else
      std::vector<Value *> killedVec(killed.begin(), killed.end());
      std::vector<Value *> tuple = {succ, MDNode::get(ctx, killedVec)};
      k.push_back(MDNode::get(ctx, tuple));
#endif
    }
#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 6)
    term->setMetadata("liveregister.killed", MDTuple::get(ctx, k));
#else
    term->setMetadata("liveregister.killed", MDNode::get(ctx, k));
#endif
  }
}

void LiveRegisterPass::generateInstructionInfo(Function &F) {
  basicBlocks.reserve(F.size());
  size_t numInstructions = 0;
  // iterate over all basic blocks
  for (Function::iterator it = F.begin(), e = F.end(); it != e; ++it) {
    BasicBlock &bb = *it;
    basicBlocks[&bb] = {};
    numInstructions += bb.size();
    instructions.reserve(numInstructions);
    // iterate over all instructions within a basic block
    for (BasicBlock::iterator it = bb.begin(), e = bb.end(); it != e; ++it) {
      Instruction *i = &*it;
      instructions[i] = {};
    }
  }

  // In the following loop, we assume that each instruction within the function
  // has a corresponding InstructionInfo entry, which is why we need to iterate
  // over all instructions twice.

  // iterate over all basic blocks
  for (Function::iterator it = F.begin(), e = F.end(); it != e; ++it) {
    BasicBlock &bb = *it;
    Instruction *previ = &*bb.begin();
    // iterate over all instructions within a basic block
    for (BasicBlock::iterator it = bb.begin(), e = bb.end(); it != e; ++it) {
      Instruction *i = &*it;
      InstructionInfo &ii = instructions[i];

      // generate predecessorEdges
      if (it == bb.begin()) {
        // first instruction in basic block: insert terminator instructions of
        // all preceding basic blocks as predecessors
        for (auto it = pred_begin(&bb), e = pred_end(&bb); it != e; ++it) {
          BasicBlock *pred = *it;
          if (Instruction *predTerm = pred->getTerminator()) {
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
        PHINode *phi = cast<PHINode>(i);

        // iterate over all incoming basic blocks
        for (auto ib = phi->block_begin(), e = phi->block_end(); ib != e; ++ib)
        {
          Value *value = phi->getIncomingValueForBlock(*ib);
          if (isa<Instruction>(value)) {
            Instruction *incomingTerm = (*ib)->getTerminator();
            InstructionInfo &incomingII = instructions[incomingTerm];
            incomingII.gen.insert(value);
          }
        }
      } else if (i->getNumOperands() != 0) {
        // iterate over all operands (uses) of the current instruction
        for (auto use = i->op_begin(), e = i->op_end(); use != e; ++use) {
          if (Instruction *op = dyn_cast<Instruction>(use->get())) {
            ii.gen.insert(op);
          }
        }
      }

      previ = i;
    }
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

Instruction *LiveRegisterPass::createNopInstruction(LLVMContext &ctx) const {
  ConstantInt *zero = ConstantInt::get(ctx, APInt(1, 0, false));
  return BinaryOperator::Create(Instruction::BinaryOps::Or, zero, zero);
}

}
