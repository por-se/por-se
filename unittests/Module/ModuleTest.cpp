//===-- ModuleTest.cpp ----------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "../lib/Module/LiveRegister.cpp"

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"

#include "../parseAssembly.h"
#include "gtest/gtest.h"

using namespace klee;

namespace klee {

TEST(ModuleTest, LiveRegisterGetLastPHI) {
  LLVMContext Ctx;

  // this program has no real functionality, it just provides some basic blocks.
  StringRef Source = "define void @test() {\n"
                     "entry:\n"
                     "  %ptr = alloca i64\n"
                     "  store i64 3, i64* %ptr\n"
                     "  %p = load i64, i64* %ptr\n"
                     "  %cmp1 = icmp ult i64 %p, 5\n"
                     "  br i1 %cmp1, label %twophi, label %onephi\n"
                     "\n"
                     "twophi:\n"
                     "  %x = phi i64 [ %p, %entry ], [ 0, %onephi ]\n"
                     "  %y = phi i64 [ 0, %entry ], [ %z, %onephi ]\n"
                     "  %cmp2 = icmp ult i64 %x, %y\n"
                     "  br i1 %cmp2, label %onephi, label %oneinst\n"
                     "\n"
                     "onephi:\n"
                     "  %c = phi i64 [ %x, %twophi ], [ 0, %entry]\n"
                     "  %z = add i64 %y, 1\n"
                     "  %cmp3 = icmp ult i64 %z, %c\n"
                     "  br i1 %cmp3, label %twophi, label %twoinst\n"
                     "\n"
                     "twoinst:\n"
                     "  %h = xor i64 0, 0\n"
                     "  ret void\n"
                     "\n"
                     "oneinst:\n"
                     "  ret void\n"
                     "}";

  auto m = parseAssembly(Ctx, Source);
  auto f = m->getFunction("test");
  for (auto &bb : *f) {
    const Instruction *firstRealInst = bb.getFirstNonPHI();
    bool hasPHI = (bb.begin()->getOpcode() == Instruction::PHI);
    ASSERT_TRUE(firstRealInst != nullptr);

    {
      // without NOP
      auto lastPHI = LiveRegisterPass::getLastPHIInstruction(bb);
      ASSERT_TRUE(lastPHI != nullptr);

      if (hasPHI) {
        // returns last PHI
        ASSERT_TRUE(lastPHI->getOpcode() == Instruction::PHI);
        ASSERT_TRUE(lastPHI->getNextNode() == firstRealInst);
      } else {
        // returns firstRealInst (no NOP present)
        ASSERT_TRUE(lastPHI->getOpcode() != Instruction::PHI);
        ASSERT_TRUE(lastPHI == firstRealInst);
      }
    }

    {
      // with NOP
      LiveRegisterPass::insertNopInstruction(bb);
      auto lastPHI = LiveRegisterPass::getLastPHIInstruction(bb);
      ASSERT_TRUE(lastPHI != nullptr);

      if (hasPHI) {
        // returns last PHI after skipping NOP
        ASSERT_TRUE(lastPHI->getOpcode() == Instruction::PHI);
        ASSERT_TRUE(lastPHI->getNextNode() == firstRealInst);
      } else {
        // returns NOP
        ASSERT_TRUE(lastPHI->getOpcode() != Instruction::PHI);
        ASSERT_TRUE(lastPHI == firstRealInst->getPrevNode());
      }
    }
  }
}

} // namespace klee
