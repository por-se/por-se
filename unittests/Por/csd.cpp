#include "por/configuration.h"
#include "por/csd.h"

#include "gtest/gtest.h"

#include <iostream>

namespace {
	TEST(CsdLimitTest, SequentialProgram1) {
		por::configuration configuration; // construct a default configuration with 1 main thread
		auto thread1 = configuration.thread_heads().begin()->second->tid();
		configuration.create_lock(thread1, 1).commit(configuration);
		configuration.acquire_lock(thread1, 1).commit(configuration);
		configuration.release_lock(thread1, 1).commit(configuration);

		ASSERT_EQ(por::compute_csd_2(*configuration.thread_heads().at(thread1)), 0u);
		ASSERT_FALSE(por::is_above_csd_limit(*configuration.thread_heads().at(thread1), 0));
	}

	TEST(CsdSearchTest, ParallelProgram1) {
		por::configuration configuration; // construct a default configuration with 1 main thread
		auto thread1 = configuration.thread_heads().begin()->second->tid();
		configuration.create_lock(thread1, 1).commit(configuration);
		configuration.acquire_lock(thread1, 1).commit(configuration);
		auto thread2 = por::thread_id{thread1, 1};
		configuration.create_thread(thread1, thread2).commit(configuration);
		configuration.init_thread(thread2, thread1).commit(configuration);
		configuration.release_lock(thread1, 1).commit(configuration);
		configuration.acquire_lock(thread2, 1).commit(configuration);

		// the csd is 0, as the context switch away from t1 was not preemptive due to t1 running out of events
		ASSERT_EQ(por::compute_csd_2(*configuration.thread_heads().at(thread2)), 0u);
		ASSERT_FALSE(por::is_above_csd_limit(*configuration.thread_heads().at(thread2), 0u));
	}

	TEST(CsdSearchTest, ParallelProgram2) {
		por::configuration configuration; // construct a default configuration with 1 main thread
		auto thread1 = configuration.thread_heads().begin()->second->tid();
		configuration.create_lock(thread1, 1).commit(configuration);
		configuration.acquire_lock(thread1, 1).commit(configuration);
		auto thread2 = por::thread_id{thread1, 1};
		configuration.create_thread(thread1, thread2).commit(configuration);
		configuration.init_thread(thread2, thread1).commit(configuration);
		configuration.release_lock(thread1, 1).commit(configuration);
		configuration.acquire_lock(thread2, 1).commit(configuration);
		configuration.exit_thread(thread2).commit(configuration);
		configuration.join_thread(thread1, thread2).commit(configuration);

		ASSERT_EQ(por::compute_csd_2(*configuration.thread_heads().at(thread1)), 0u);
		ASSERT_FALSE(por::is_above_csd_limit(*configuration.thread_heads().at(thread1), 0u));
	}

	TEST(CsdTest, ParallelProgram3) {
		por::configuration configuration; // construct a default configuration with 1 main thread
		auto thread1 = configuration.thread_heads().begin()->second->tid();
		configuration.create_lock(thread1, 1).commit(configuration);
		auto thread2 = por::thread_id{thread1, 1};
		configuration.create_thread(thread1, thread2).commit(configuration);
		configuration.init_thread(thread2, thread1).commit(configuration);
		configuration.acquire_lock(thread2, 1).commit(configuration);
		configuration.release_lock(thread2, 1).commit(configuration);
		configuration.acquire_lock(thread1, 1).commit(configuration);
		configuration.release_lock(thread1, 1).commit(configuration);
		configuration.exit_thread(thread2).commit(configuration);
		configuration.join_thread(thread1, thread2).commit(configuration);

		ASSERT_EQ(por::compute_csd_2(*configuration.thread_heads().at(thread1)), 1u);
		ASSERT_FALSE(por::is_above_csd_limit(*configuration.thread_heads().at(thread1), 1u));
		ASSERT_TRUE(por::is_above_csd_limit(*configuration.thread_heads().at(thread1), 0u));
	}
} // namespace
