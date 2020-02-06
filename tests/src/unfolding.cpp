#include "por/configuration.h"
#include "por/thread_id.h"

#include <cassert>
#include <iostream>

namespace {
	bool regression_deduplication_lid() {
		por::configuration configuration1; // construct a default configuration with 1 main thread
		por::configuration configuration2 = configuration1; // create copy within same unfolding
		assert(configuration1.unfolding() && configuration1.unfolding() == configuration2.unfolding());
		auto thread1 = configuration1.thread_heads().begin()->second->tid();
		assert(thread1 = configuration2.thread_heads().begin()->second->tid());

		auto acq1 = configuration1.acquire_lock(thread1, 1);
		auto acq2 = configuration2.acquire_lock(thread1, 2);
		return acq1 != acq2;
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

	run_test(regression_deduplication_lid)

	if(result == 0) {
		::std::cout << "\n\x1B[32mOK.\x1B[0m\n";
	} else {
		::std::cout << "\n\x1B[31m" << result << " test failures!\x1B[0m\n";
	}

	return result;
}
