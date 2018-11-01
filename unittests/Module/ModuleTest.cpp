//===-- ModuleTest.cpp ----------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "../lib/Module/Passes.h"

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"

#include "../parseAssembly.h"
#include "gtest/gtest.h"

using namespace llvm;

namespace klee {

class LiveRegisterPassTest : public ::testing::Test {
private:
  LLVMContext Ctx;
  std::unique_ptr<Module> m;

protected:
  Function *testFunction;

  void SetUp() override {
    // no real functionality, just providing some basic blocks
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

    m = parseAssembly(Ctx, Source);
    testFunction = m->getFunction("test");
  }

  void TearDown() override {
    testFunction = nullptr;
    m.reset();
  }
};

TEST_F(LiveRegisterPassTest, GetLastPHI) {
  for (auto &bb : *testFunction) {
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

TEST_F(LiveRegisterPassTest, GetLiveSet) {
  LiveRegisterPass lrp;
  lrp.runOnFunction(*testFunction);

  std::set<const Instruction *> instSet;
  for (auto &bb : *testFunction) {
    for (auto &i : bb) {
      instSet.insert(&i);
    }
  }

  std::size_t validLiveSets = 0;

  // test whether all live sets that should be valid are valid
  for (auto &bb : *testFunction) {
    const Instruction *firstRealInst = bb.getFirstNonPHI();
    for (auto &i : bb) {
      auto liveSet = lrp.getLiveSet(&i);
      bool shouldBeValid = (i.getOpcode() != Instruction::PHI);
      shouldBeValid |= (i.getNextNode() == firstRealInst);

      if (shouldBeValid) {
        ++validLiveSets;
        ASSERT_TRUE(liveSet != nullptr);

        // test whether live set only contains valid values
        for (auto &v : *liveSet) {
          const Instruction *inst = static_cast<const Instruction *>(v);
          ASSERT_TRUE(instSet.find(inst) != instSet.end());
          ASSERT_TRUE(inst->getParent()->getParent() == testFunction);
        }
      } else {
        ASSERT_TRUE(liveSet == nullptr);
      }
    }
  }

  // test invalid Instruction
  ASSERT_TRUE(lrp.getLiveSet(nullptr) == nullptr);
  const Instruction *invalid = reinterpret_cast<Instruction *>(0x42);
  while (lrp.instructions.count(invalid) == 1)
    ++invalid;
  ASSERT_TRUE(lrp.getLiveSet(invalid) == nullptr);

  // test whether only reachable instructions are valid
  for (auto &item : lrp.instructions) {
    auto key = item.first;
    auto ii = item.second;

    if (lrp.getLiveSet(key) != nullptr) {
      --validLiveSets;
    }
  }
  ASSERT_TRUE(validLiveSets == 0);
}

} // namespace klee
