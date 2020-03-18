#include "por/configuration.h"
#include "por/csd.h"

#include "gtest/gtest.h"

#include <iostream>

namespace {
	TEST(CsdTest, SequentialProgram1) {
		por::configuration configuration; // construct a default configuration with 1 main thread
		auto thread1 = configuration.thread_heads().begin()->second->tid();
		configuration.create_lock(thread1, 1);
		configuration.acquire_lock(thread1, 1);
		configuration.release_lock(thread1, 1);

		ASSERT_FALSE(por::is_above_csd_limit(*configuration.thread_heads().at(thread1), 1));
	}

	TEST(CsdTest, SequentialProgram2) {
		por::configuration configuration; // construct a default configuration with 1 main thread
		auto thread1 = configuration.thread_heads().begin()->second->tid();
		configuration.create_lock(thread1, 1);
		configuration.acquire_lock(thread1, 1);
		configuration.release_lock(thread1, 1);

		ASSERT_TRUE(por::is_above_csd_limit(*configuration.thread_heads().at(thread1), 0));
	}

	TEST(CsdTest, ParallelProgram1) {
		por::configuration configuration; // construct a default configuration with 1 main thread
		auto thread1 = configuration.thread_heads().begin()->second->tid();
		configuration.create_lock(thread1, 1);
		configuration.acquire_lock(thread1, 1);
		auto thread2 = por::thread_id{thread1, 1};
		configuration.create_thread(thread1, thread2);
		configuration.init_thread(thread2, thread1);
		configuration.release_lock(thread1, 1);
		configuration.acquire_lock(thread2, 1);

		ASSERT_FALSE(por::is_above_csd_limit(*configuration.thread_heads().at(thread2), 2));
	}

	TEST(CsdTest, ParallelProgram2) {
		por::configuration configuration; // construct a default configuration with 1 main thread
		auto thread1 = configuration.thread_heads().begin()->second->tid();
		configuration.create_lock(thread1, 1);
		configuration.acquire_lock(thread1, 1);
		auto thread2 = por::thread_id{thread1, 1};
		configuration.create_thread(thread1, thread2);
		configuration.init_thread(thread2, thread1);
		configuration.release_lock(thread1, 1);
		configuration.acquire_lock(thread2, 1);

		ASSERT_TRUE(por::is_above_csd_limit(*configuration.thread_heads().at(thread2), 1));
	}

	TEST(CsdTest, ParallelProgram3) {
		por::configuration configuration; // construct a default configuration with 1 main thread
		auto thread1 = configuration.thread_heads().begin()->second->tid();
		configuration.create_lock(thread1, 1);
		configuration.acquire_lock(thread1, 1);
		auto thread2 = por::thread_id{thread1, 1};
		configuration.create_thread(thread1, thread2);
		configuration.init_thread(thread2, thread1);
		configuration.release_lock(thread1, 1);
		configuration.acquire_lock(thread2, 1);
		configuration.exit_thread(thread2);
		configuration.join_thread(thread1, thread2);

		ASSERT_FALSE(por::is_above_csd_limit(*configuration.thread_heads().at(thread1), 3));
	}

	TEST(CsdTest, ParallelProgram4) {
		por::configuration configuration; // construct a default configuration with 1 main thread
		auto thread1 = configuration.thread_heads().begin()->second->tid();
		configuration.create_lock(thread1, 1);
		configuration.acquire_lock(thread1, 1);
		auto thread2 = por::thread_id{thread1, 1};
		configuration.create_thread(thread1, thread2);
		configuration.init_thread(thread2, thread1);
		configuration.release_lock(thread1, 1);
		configuration.acquire_lock(thread2, 1);
		configuration.exit_thread(thread2);
		configuration.join_thread(thread1, thread2);

		ASSERT_TRUE(por::is_above_csd_limit(*configuration.thread_heads().at(thread1), 2));
	}
} // namespace
