#include <por/configuration.h>
#include <por/csd.h>

#include <iostream>

namespace {
	bool sequential_program_1() {
		por::configuration configuration; // construct a default configuration with 1 main thread
		auto thread1 = configuration.thread_heads().begin()->second->tid();
		configuration.create_lock(thread1, 1);
		configuration.acquire_lock(thread1, 1);
		configuration.release_lock(thread1, 1);

		return false == por::is_above_csd_limit(*configuration.thread_heads().at(thread1), 1);
	}

	bool sequential_program_2() {
		por::configuration configuration; // construct a default configuration with 1 main thread
		auto thread1 = configuration.thread_heads().begin()->second->tid();
		configuration.create_lock(thread1, 1);
		configuration.acquire_lock(thread1, 1);
		configuration.release_lock(thread1, 1);

		return true == por::is_above_csd_limit(*configuration.thread_heads().at(thread1), 0);
	}

	bool parallel_program_1() {
		por::configuration configuration; // construct a default configuration with 1 main thread
		auto thread1 = configuration.thread_heads().begin()->second->tid();
		configuration.create_lock(thread1, 1);
		configuration.acquire_lock(thread1, 1);
		auto thread2 = por::thread_id{thread1, 1};
		configuration.create_thread(thread1, thread2);
		configuration.init_thread(thread2);
		configuration.release_lock(thread1, 1);
		configuration.acquire_lock(thread2, 1);

		return false == por::is_above_csd_limit(*configuration.thread_heads().at(thread2), 2);
	}

	bool parallel_program_2() {
		por::configuration configuration; // construct a default configuration with 1 main thread
		auto thread1 = configuration.thread_heads().begin()->second->tid();
		configuration.create_lock(thread1, 1);
		configuration.acquire_lock(thread1, 1);
		auto thread2 = por::thread_id{thread1, 1};
		configuration.create_thread(thread1, thread2);
		configuration.init_thread(thread2);
		configuration.release_lock(thread1, 1);
		configuration.acquire_lock(thread2, 1);

		return true == por::is_above_csd_limit(*configuration.thread_heads().at(thread2), 1);
	}

	bool parallel_program_3() {
		por::configuration configuration; // construct a default configuration with 1 main thread
		auto thread1 = configuration.thread_heads().begin()->second->tid();
		configuration.create_lock(thread1, 1);
		configuration.acquire_lock(thread1, 1);
		auto thread2 = por::thread_id{thread1, 1};
		configuration.create_thread(thread1, thread2);
		configuration.init_thread(thread2);
		configuration.release_lock(thread1, 1);
		configuration.acquire_lock(thread2, 1);
		configuration.exit_thread(thread2);
		configuration.join_thread(thread1, thread2);

		return false == por::is_above_csd_limit(*configuration.thread_heads().at(thread1), 3);
	}

	bool parallel_program_4() {
		por::configuration configuration; // construct a default configuration with 1 main thread
		auto thread1 = configuration.thread_heads().begin()->second->tid();
		configuration.create_lock(thread1, 1);
		configuration.acquire_lock(thread1, 1);
		auto thread2 = por::thread_id{thread1, 1};
		configuration.create_thread(thread1, thread2);
		configuration.init_thread(thread2);
		configuration.release_lock(thread1, 1);
		configuration.acquire_lock(thread2, 1);
		configuration.exit_thread(thread2);
		configuration.join_thread(thread1, thread2);

		return true == por::is_above_csd_limit(*configuration.thread_heads().at(thread1), 2);
	}
}

#define run_test(fun) \
	std::cout << "Running unit test " #fun "...\n"; \
	if(!(fun)()) { \
		std::cerr << "\x1B[31mUnit test " #fun " failed.\x1B[0m\n"; \
		++result; \
	}

int main() {
	int result = 0;

	run_test(sequential_program_1)
	run_test(sequential_program_2)
	run_test(parallel_program_1)
	run_test(parallel_program_2)
	run_test(parallel_program_3)
	run_test(parallel_program_4)

	if(result == 0) {
		::std::cout << "\n\x1B[32mOK.\x1B[0m\n";
	} else {
		::std::cout << "\n\x1B[31m" << result << " test failures!\x1B[0m\n";
	}

	return result;
}
