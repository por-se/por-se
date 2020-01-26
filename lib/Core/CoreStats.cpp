//===-- CoreStats.cpp -----------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "CoreStats.h"

using namespace klee;

Statistic stats::allocations("Allocations", "Alloc");
Statistic stats::coveredInstructions("CoveredInstructions", "Icov");
Statistic stats::falseBranches("FalseBranches", "Bf");
Statistic stats::forkTime("ForkTime", "Ftime");
Statistic stats::forks("Forks", "Forks");
Statistic stats::instructionRealTime("InstructionRealTimes", "Ireal");
Statistic stats::instructionTime("InstructionTimes", "Itime");
Statistic stats::instructions("Instructions", "I");
Statistic stats::minDistToReturn("MinDistToReturn", "Rdist");
Statistic stats::minDistToUncovered("MinDistToUncovered", "UCdist");
Statistic stats::reachableUncovered("ReachableUncovered", "IuncovReach");
Statistic stats::resolveTime("ResolveTime", "Rtime");
Statistic stats::solverTime("SolverTime", "Stime");
Statistic stats::states("States", "States");
Statistic stats::trueBranches("TrueBranches", "Bt");
Statistic stats::uncoveredInstructions("UncoveredInstructions", "Iuncov");

Statistic stats::catchUpInstructions("CatchUpInstructions", "Icup");
Statistic stats::standbyStates("StandbyStates", "Standby");
Statistic stats::maxConfigurations("MaximalConfigurations", "Mconf");
Statistic stats::cutoffEvents("CutoffEvents", "coE");
Statistic stats::deadlockedExecutions("DeadlockedExecutions", "Dl");
Statistic stats::cutoffConfigurations("CutoffConfigurations", "coConf");
Statistic stats::exitedConfigurations("ExitedConfigurations", "eConf");
Statistic stats::errorConfigurations("ErrorConfigurations", "errConf");
Statistic stats::dataraceConfigurations("DataraceConfigurations", "drConf");
