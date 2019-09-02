#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <utility>

#include <sys/mman.h>
#include <unistd.h>

#if defined(PSEUDOALLOC_CHECKED)
#	define _pa_check(expr) (assert(expr))
#else
#	define _pa_check(expr) ((void)0)
#endif

namespace pseudoalloc {
#if defined(PSEUDOALLOC_CHECKED)
	static constexpr const bool checked_build = true;
#else
	static constexpr const bool checked_build = false;
#endif

	class mapping_t {
		void* _begin = MAP_FAILED;
		std::size_t _size = 0;

	public:
		mapping_t(std::size_t size)
		  : mapping_t(0, size) {}

		mapping_t(std::uintptr_t address, std::size_t size)
		  : _size(size) {
			int flags = MAP_ANON | MAP_PRIVATE | MAP_NORESERVE;
			if(address != 0) {
#if defined(__APPLE__)
				flags |= MAP_FIXED;
#else
				flags |= MAP_FIXED_NOREPLACE;
#endif
			}

			_begin = ::mmap(reinterpret_cast<void*>(address), _size, PROT_READ | PROT_WRITE, flags, -1, 0);
			assert(_begin != MAP_FAILED);
			assert(address == 0 || address == reinterpret_cast<std::uintptr_t>(_begin));

#if defined(__linux__)
			{
				[[maybe_unused]] int rc = ::madvise(_begin, _size, MADV_DONTFORK | MADV_RANDOM);
				assert(rc == 0 && "madvise failed");
			}
#endif
		}

		mapping_t(mapping_t const&) = delete;
		mapping_t& operator=(mapping_t const&) = delete;
		mapping_t(mapping_t&&) = delete;
		mapping_t& operator=(mapping_t&&) = delete;

		[[nodiscard]] void* begin() const noexcept { return _begin; }
		[[nodiscard]] std::size_t size() const noexcept { return _size; }

		void clear() {
			[[maybe_unused]] int rc = ::madvise(_begin, _size, MADV_DONTNEED);
			assert(rc == 0 && "madvise failed");
		}

		~mapping_t() {
			[[maybe_unused]] int rc = ::munmap(_begin, _size);
			assert(rc == 0 && "munmap failed");
		}
	};

	namespace util {
		[[nodiscard]] static constexpr inline int clz(unsigned const x) noexcept {
			_pa_check(x > 0);
			return __builtin_clz(x);
		}

		[[nodiscard]] static constexpr inline int clz(unsigned long const x) noexcept {
			_pa_check(x > 0);
			return __builtin_clzl(x);
		}

		[[nodiscard]] static constexpr inline int clz(unsigned long long const x) noexcept {
			_pa_check(x > 0);
			return __builtin_clzll(x);
		}

		[[nodiscard]] static constexpr inline int ctz(unsigned const x) noexcept {
			_pa_check(x > 0);
			return __builtin_ctz(x);
		}

		[[nodiscard]] static constexpr inline int ctz(unsigned long const x) noexcept {
			_pa_check(x > 0);
			return __builtin_ctzl(x);
		}

		[[nodiscard]] static constexpr inline int ctz(unsigned long long const x) noexcept {
			_pa_check(x > 0);
			return __builtin_ctzll(x);
		}

		[[nodiscard]] static constexpr inline std::size_t round_up_to_multiple_of_4096(std::size_t const x) {
			return ((x - 1) | static_cast<std::size_t>(4096 - 1)) + 1;
		}
	} // namespace util

	/// Wraps a mapping that is shared with other allocators that are required to return
	/// identical addresses. The bins are maintained in the following layout in the mapping:
	///
	/// +------------------------------------------------+
	/// | 8 | 16 | ... | 4096 | large object bin | stack |
	/// +------------------------------------------------+
	class allocator_t {
		class sized_bin_t {
			/// Stores the slots which are currently allocated as a sorted sequence of right-open intervals, e.g., ((1, 3),
			/// (4, 5)) means that the slots 1, 2 and 4 are currently in use. Between two of these intervals, there is always
			/// at least one empty slot (i.e., ((1,2), (2,3)) is not valid and will not occur).
			std::map<std::size_t, std::size_t> _allocated;

			char* _base = nullptr;
			std::size_t _size = 0;
			std::size_t _slot_size = 0;

			[[nodiscard]] inline std::size_t index2pos(std::size_t index) const {
				index += 1;
				int layer = std::numeric_limits<std::size_t>::digits - util::clz(index);
				auto high_bit = static_cast<std::size_t>(1) << (layer - 1);
				_pa_check((high_bit & index) != 0 && "Failed to compute high bit");

				auto current_slot_size = (_size >> layer);
				assert(current_slot_size > _slot_size && "Zero (or below) red zone size!");
				auto pos = (index ^ high_bit) * 2 + 1;
				return current_slot_size * pos;
			}

			[[nodiscard]] inline std::size_t pos2index(std::size_t pos) const noexcept {
				int trailing_zeroes = util::ctz(pos);
				auto layer_index = pos >> (trailing_zeroes + 1);
				auto layer = util::ctz(_size) - (trailing_zeroes + 1);
				auto result = (static_cast<std::size_t>(1) << layer) + layer_index - 1;
				return result;
			}

		public:
			void initialize(void* base, std::size_t size, std::size_t slot_size) noexcept {
				_pa_check(size > 0 && (size & (size - 1)) == 0 && "Sizes of sized bins must be powers of two");

				_base = static_cast<char*>(base);
				_size = size;
				_slot_size = slot_size;
			}

			[[nodiscard]] void* allocate() {
				if(_allocated.empty()) {
					_allocated.emplace(0, 1);
					return _base + index2pos(0);
				}

				auto it = _allocated.begin();
				auto const index = it->second++;
				if(auto next = std::next(it); next != _allocated.end() && next->first == index + 1) {
					it->second = next->second;
					_allocated.erase(next);
				}
				return _base + index2pos(index);
			}

			void deallocate(void* const ptr) {
				auto pos = static_cast<std::size_t>(static_cast<char*>(ptr) - _base);
				_pa_check(pos < _size);
				auto index = pos2index(pos);

				auto const next = _allocated.upper_bound(index);
				auto it = std::prev(next);
				assert(it != _allocated.end() && it->second > index && "Invalid free");
				if(index == it->first) {
					if(index + 1 == it->second) {
						_allocated.erase(it);
					} else {
						auto node = _allocated.extract(it);
						_pa_check(!node.empty());
						++node.key();
						_allocated.insert(next, std::move(node));
						_pa_check(node.empty());
					}
				} else if (index + 1 == it->second) {
					_pa_check(index != it->first);
					--it->second;
				} else {
					_allocated.emplace_hint(next, index + 1, it->second);
					it->second = index;
				}
			}
		};

		/// The large object heap is implemented as what amounts to as a bi-directional mapping between the position of
		/// each unallocated region and its actual size. The implemented algorithm performs allocations in the middle of
		/// the largest available unallocated region. Allocations are guaranteed to be aligned to 4096 bytes.
		class large_object_heap_t {
			/// The first direction of the mapping, `map1`, maps the size of each free region to its location. It is inversely
			/// sorted by size, so that `map1.begin()` always returns the largest free region.
			/// As it is possible that two free ranges have the same size, there is a need to provide a deterministic
			/// tie-breaker between those. To this end, the mapping is implemented as a set of pairs of size and beginning of
			/// the free ranges. This way, sorting is done first by size, and - for ranges of the same size - second on their
			/// position.
			std::set<std::pair<std::size_t, char*>, std::greater<std::pair<std::size_t, char*>>> map1;

			/// The second direction of the mapping, `map2`, maps the position of each free region to its size. Its ordering
			/// is ascending, so that a lookup for the position of an allocated object is able to return the previous and the
			/// following unallocated region.
			std::map<char*, std::size_t> map2;

		public:
			void initialize(void* base, std::size_t size) {
				map1.emplace(size, static_cast<char*>(base));
				map2.emplace(static_cast<char*>(base), size);
			}

			[[nodiscard]] void* allocate(std::size_t size) {
				_pa_check(size > 4096);
				size = util::round_up_to_multiple_of_4096(size);

				_pa_check(!map1.empty());
				auto largest_free_range = map1.extract(map1.begin());
				_pa_check(!largest_free_range.empty());
				std::size_t const range_size = largest_free_range.value().first;
				char* const range_pos = largest_free_range.value().second;
				assert(range_size + 2 * 4096 >= size && "Zero (or below) red zone size!");

				auto offset = (range_size - size) / 2;
				offset = util::round_up_to_multiple_of_4096(offset);

				// left subrange
				map1.emplace(offset, range_pos);
				auto finger = map2.find(range_pos);
				_pa_check(finger != map2.end());
				_pa_check(finger->second == range_size);
				finger->second = offset;

				// right subrange
				largest_free_range.value() =
				  std::pair<std::size_t, char*>(range_size - offset - size, range_pos + offset + size);
				map1.insert(std::move(largest_free_range));
				map2.emplace_hint(std::next(finger), range_pos + offset + size, range_size - offset - size);

				return range_pos + offset;
			}

			void deallocate(void* ptr, std::size_t size) {
				_pa_check(size > 4096);
				size = util::round_up_to_multiple_of_4096(size);

				auto right_it = map2.upper_bound(static_cast<char*>(ptr));
				auto left_it = std::prev(right_it);
				_pa_check(left_it->first + left_it->second == static_cast<char*>(ptr));
				_pa_check(left_it->first + left_it->second + size == right_it->first);

				[[maybe_unused]] auto erased = map1.erase(std::pair<std::size_t, char*>(right_it->second, right_it->first));
				_pa_check(erased == 1);
				auto node = map1.extract(std::pair<std::size_t, char*>(left_it->second, left_it->first));
				_pa_check(!node.empty());
				std::size_t const combined_size = left_it->second + size + right_it->second;
				node.value() = std::pair<std::size_t, char*>(combined_size, left_it->first);
				map1.insert(std::move(node));

				left_it->second = combined_size;
				map2.erase(right_it);
			}
		};

		mapping_t* _mapping;
		std::array<sized_bin_t, 10> _sized_bins;
		large_object_heap_t _loh;

		[[nodiscard]] static inline int size2bin(std::size_t size) noexcept {
			if(size <= 8) {
				return 0;
			} else if(size > 4096) {
				return 10;
			} else {
				int result = (std::numeric_limits<std::size_t>::digits - 3) - util::clz(size - 1);
				_pa_check(result > 0 && result < 10);
				return result;
			}
		}

	public:
		allocator_t(mapping_t& mapping)
		  : _mapping(&mapping) {
			assert(mapping.size() > 11 && "Mapping is *far* to small");

			auto bin_size = static_cast<std::size_t>(1)
			                << (std::numeric_limits<std::size_t>::digits - 1 - util::clz(_mapping->size() / 11));
			char* const base = static_cast<char*>(_mapping->begin());
			std::size_t slot_size = 8;
			std::size_t total_size = 0;
			for(auto& bin : _sized_bins) {
				bin.initialize(base + total_size, bin_size, slot_size);

				total_size += bin_size;
				assert(total_size <= _mapping->size() && "Mapping too small");
				slot_size *= 2;
			}

			auto loh_size = mapping.size() - total_size;
			assert(loh_size > 0);
			_loh.initialize(base + total_size, loh_size);
		}

		allocator_t(allocator_t const&) = default;
		allocator_t& operator=(allocator_t const&) = default;
		allocator_t(allocator_t&&) = default;
		allocator_t& operator=(allocator_t&&) = default;

		[[nodiscard]] void* allocate(std::size_t size) {
			if(auto bin = size2bin(size); bin < 10) {
				return _sized_bins[bin].allocate();
			} else {
				return _loh.allocate(size);
			}
		}

		void free(void* ptr, std::size_t size) {
			assert(ptr && "Freeing nullptrs is not supported"); // we are not ::free!

			if(auto bin = size2bin(size); bin < 10) {
				return _sized_bins[bin].deallocate(ptr);
			} else {
				return _loh.deallocate(ptr, size);
			}
		}
	};
} // namespace pseudoalloc