#include "pseudoalloc/pseudoalloc.h"
#include "xoshiro.h"

#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <random>
#include <utility>
#include <vector>

namespace {
	class RandomTest {
		xoshiro512 rng;

#if defined(USE_PSEUDOALLOC)
		pseudoalloc::mapping_t mapping;
		pseudoalloc::stack_allocator_t allocator;
#endif

		std::vector<std::pair<void*, std::size_t>> allocations;

		std::geometric_distribution<std::size_t> allocation_bin_distribution;
		std::geometric_distribution<std::size_t> large_allocation_distribution;

	public:
		std::size_t maximum_concurrent_allocations = 0;
		std::uint64_t allocation_count = 0;
		std::uint64_t deallocation_count = 0;

		RandomTest(std::mt19937_64::result_type seed = 0x31337)
		  : rng(seed)
#if defined(USE_PSEUDOALLOC)
		  , mapping(static_cast<std::size_t>(1) << 44)
		  , allocator(mapping)
#endif
		  , allocation_bin_distribution(0.3)
		  , large_allocation_distribution(0.00003) {
		}

		void run(std::uint64_t const iterations) {
			allocations.reserve((iterations * 7) / 10);
			std::uniform_int_distribution<std::uint32_t> choice(0, 999);
			for(std::uint64_t i = 0; i < iterations; ++i) {
				auto chosen = choice(rng);
				if(chosen < 650) {
					++allocation_count;
					allocate_sized();
				} else if(chosen < 700) {
					++allocation_count;
					allocate_large();
				} else if(chosen < 1000) {
					++deallocation_count;
					deallocate();
				}
			}
			cleanup();
		}

		void cleanup() {
			while(!allocations.empty()) {
#if defined(USE_PSEUDOALLOC)
				allocator.free(allocations.back().first, allocations.back().second);
#else
				free(allocations.back().first);
#endif
				allocations.pop_back();
			}
		}

		void allocate_sized() {
			auto bin = allocation_bin_distribution(rng);
			while(bin >= 11) {
				bin = allocation_bin_distribution(rng);
			}
			auto min = (bin == 0 ? 1 : (static_cast<std::size_t>(1) << (bin + 1)) + 1);
			auto max = static_cast<std::size_t>(1) << (bin + 2);
			auto size = std::uniform_int_distribution<std::size_t>(min, max)(rng);

#if defined(USE_PSEUDOALLOC)
			allocations.emplace_back(allocator.allocate(size), size);
#else
			allocations.emplace_back(malloc(size), size);
#endif
			if(allocations.size() > maximum_concurrent_allocations) {
				maximum_concurrent_allocations = allocations.size();
			}
		}

		void allocate_large() {
			auto size = 0;
			while(size <= 4096 || size > 1073741825) {
				size = large_allocation_distribution(rng) + 4097;
			}

#if defined(USE_PSEUDOALLOC)
			allocations.emplace_back(allocator.allocate(size), size);
#else
			allocations.emplace_back(malloc(size), size);
#endif
			if(allocations.size() > maximum_concurrent_allocations) {
				maximum_concurrent_allocations = allocations.size();
			}
		}

		void deallocate() {
			if(allocations.empty()) {
				return;
			}
#if defined(USE_PSEUDOALLOC)
			allocator.free(allocations.back().first, allocations.back().second);
#else
			free(allocations.back().first);
#endif
			allocations.pop_back();
		}
	};
} // namespace

int main() {
#if defined(USE_PSEUDOALLOC)
	std::cout << "Using pseudoalloc" << (pseudoalloc::checked_build ? " (checked)" : " (unchecked)") << "\n";
#else
	std::cout << "Using ::malloc\n";
#endif
	auto start = std::chrono::steady_clock::now();

	RandomTest tester;
	tester.run(50'000'000);

	auto stop = std::chrono::steady_clock::now();
	std::cout << std::dec << std::chrono::duration_cast<std::chrono::milliseconds>(stop - start).count() << " ms\n";
	std::cout << "\n";

	std::cout << "Allocations: " << tester.allocation_count << "\n";
	std::cout << "Deallocations: " << tester.deallocation_count << "\n";
	std::cout << "Maximum concurrent allocations: " << tester.maximum_concurrent_allocations << "\n";
}
