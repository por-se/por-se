#include "pseudoalloc/pseudoalloc.h"

#if defined(USE_GTEST_INSTEAD_OF_MAIN)
#	include "gtest/gtest.h"
#endif

#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <unordered_set>

#if defined(USE_GTEST_INSTEAD_OF_MAIN)
int reuse_test() {
#else
int main() {
#endif
	auto mapping = pseudoalloc::mapping_t(static_cast<std::size_t>(1) << 42);

	{
		static const std::size_t size = 8;
		static const std::uint32_t quarantine = 0;
		std::cout << "size = " << size << " quarantine = " << quarantine << "\n";
		auto allocator = pseudoalloc::allocator_t(mapping, quarantine);
		auto a = allocator.allocate(size);
		allocator.free(a, size);
		auto b = allocator.allocate(size);
		allocator.free(b, size);
		auto c = allocator.allocate(size);
		allocator.free(c, size);
		auto d = allocator.allocate(size);
		allocator.free(d, size);

		std::cout << "a: " << a << "\nb: " << b << "\nc: " << c << "\nd: " << d << "\n";
		assert(a == b);
		assert(a == c);
		assert(a == d);
	}

	std::cout << "\n\n";

	{
		static const std::size_t size = 8;
		static const std::uint32_t quarantine = 1;
		std::cout << "size = " << size << " quarantine = " << quarantine << "\n";
		auto allocator = pseudoalloc::allocator_t(mapping, quarantine);
		auto a = allocator.allocate(size);
		allocator.free(a, size);
		auto b = allocator.allocate(size);
		allocator.free(b, size);
		auto c = allocator.allocate(size);
		allocator.free(c, size);
		auto d = allocator.allocate(size);
		allocator.free(d, size);

		std::cout << "a: " << a << "\nb: " << b << "\nc: " << c << "\nd: " << d << "\n";
		assert(a != b);
		assert(a == c);
		assert(b == d);
	}

	std::cout << "\n\n";

	{
		static const std::size_t size = 8;
		static const std::uint32_t quarantine = 2;
		std::cout << "size = " << size << " quarantine = " << quarantine << "\n";
		auto allocator = pseudoalloc::allocator_t(mapping, quarantine);
		auto a = allocator.allocate(size);
		allocator.free(a, size);
		auto b = allocator.allocate(size);
		allocator.free(b, size);
		auto c = allocator.allocate(size);
		allocator.free(c, size);
		auto d = allocator.allocate(size);
		allocator.free(d, size);

		std::cout << "a: " << a << "\nb: " << b << "\nc: " << c << "\nd: " << d << "\n";
		assert(a != b);
		assert(a != c);
		assert(b != c);
		assert(a == d);
	}

	std::exit(0);
}

#if defined(USE_GTEST_INSTEAD_OF_MAIN)
TEST(PseudoallocDeathTest, Reuse) { ASSERT_EXIT(reuse_test(), ::testing::ExitedWithCode(0), ""); }
#endif
