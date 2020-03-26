#include "por/configuration.h"
#include "por/thread_id.h"

#include "gtest/gtest.h"

#include <iostream>

namespace {
	TEST(UnfoldingTest, RegressionDeduplicationLid) {
		por::configuration configuration1; // construct a default configuration with 1 main thread
		por::configuration configuration2 = configuration1; // create copy within same unfolding
		ASSERT_TRUE(configuration1.unfolding() && configuration1.unfolding() == configuration2.unfolding());
		auto thread1 = configuration1.thread_heads().begin()->second->tid();
		ASSERT_EQ(thread1, configuration2.thread_heads().begin()->second->tid());

		auto acq1 = configuration1.acquire_lock(thread1, 1).commit(configuration1);
		auto acq2 = configuration2.acquire_lock(thread1, 2).commit(configuration2);
		ASSERT_NE(acq1, acq2);
	}
}
