#include "pseudoalloc/pseudoalloc.h"

#if defined(USE_GTEST_INSTEAD_OF_MAIN)
	#include "gtest/gtest.h"
#endif

#include <cassert>

#if defined(USE_GTEST_INSTEAD_OF_MAIN)
int sample_test() {
#else
int main() {
#endif
	// initialize a mapping and an associated allocator (using the location "0" gives an OS-assigned location)
	pseudoalloc::mapping_t mapping(static_cast<std::size_t>(1) << 40);
	pseudoalloc::allocator_t allocator(mapping, 0); /// allocator without a quarantine zone

	// let us create an integer
	void* ptr = allocator.allocate(sizeof(int));
	int* my_int = static_cast<int*>(ptr);
	*my_int = 42;
	assert(*my_int == 42 && "While we can use the addresses, this is not the point of pseudoalloc");

	{
		auto allocator2 = pseudoalloc::allocator_t(mapping, 0); /// allocator without a quarantine zone
		int* my_second_int = static_cast<int*>(allocator2.allocate(sizeof(int)));
		assert(reinterpret_cast<std::uintptr_t>(my_int) == reinterpret_cast<std::uintptr_t>(my_second_int) &&
		       "pseudoalloc is intended to produce reproducible addresses");
		allocator2.free(my_second_int, sizeof(int));
		assert(*my_int == 42 && "The original allocation (from allocator) is still valid");
	}

	// now let us clone the allocator state
	{
		auto allocator2 = allocator;
		int* my_second_int = static_cast<int*>(allocator2.allocate(sizeof(int)));
		assert(reinterpret_cast<std::uintptr_t>(my_int) != reinterpret_cast<std::uintptr_t>(my_second_int) &&
		       "the new address must be different, as allocator2 also contains the previous allocation");
		allocator2.free(my_second_int, sizeof(int));
		assert(*my_int == 42 && "The original allocation (from allocator) is still valid");
	}

	// there is no need to return allocated memory, so we omit `allocator.free(my_int, sizeof(int));`
	exit(0);
}

#if defined(USE_GTEST_INSTEAD_OF_MAIN)
TEST(PseudoallocDeathTest, Sample) { ASSERT_EXIT(sample_test(), ::testing::ExitedWithCode(0), ""); }
#endif
