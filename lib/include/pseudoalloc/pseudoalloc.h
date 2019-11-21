#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <utility>
#include <vector>

#include <sys/mman.h>
#include <unistd.h>

#if defined(__linux)
#	include <linux/version.h>
#endif

#if defined(PSEUDOALLOC_CHECKED)
#	define _pa_check(expr) (assert(expr))
#else
#	define _pa_check(expr) ((void)0)
#endif

#if defined(PSEUDOALLOC_TRACE)
#	include <iostream>
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
#	if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0)
				flags |= MAP_FIXED_NOREPLACE;
#	else
				flags |= MAP_FIXED;
#	endif
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

		class quarantine_t {
			std::unique_ptr<void*[]> _data;
			std::uint32_t _capacity = 0;
			std::uint32_t _pos = 0;

		public:
			quarantine_t() = default;
			quarantine_t(quarantine_t const& rhs)
			  : _capacity(rhs._capacity)
			  , _pos(rhs._pos) {
				if(_capacity > 0 && rhs._data) {
					_data.reset(new void*[_capacity]);
					for(std::size_t i = 0; i < _capacity; ++i) {
						_data[i] = rhs._data[i];
					}
				}
			}
			quarantine_t& operator=(quarantine_t const& rhs) {
				if(this != &rhs) {
					if(_capacity < rhs._capacity) {
						_data.reset();
					}
					_pos = rhs._pos;
					_capacity = rhs._capacity;
					if(_capacity > 0 && rhs._data) {
						if(!_data) {
							_data.reset(new void*[_capacity]);
						}
						for(std::size_t i = 0; i < _capacity; ++i) {
							_data[i] = rhs._data[i];
						}
					}
				}
				return *this;
			}
			quarantine_t(quarantine_t&&) = default;
			quarantine_t& operator=(quarantine_t&&) = default;

			void initialize(std::uint32_t const capacity) {
				_pa_check(!_data);
				_pa_check(!_capacity);
				_pa_check(!_pos);

				_capacity = capacity;
			}

			std::uint32_t capacity() const noexcept { return _capacity; }

			void* deallocate(void* const ptr) {
				if(_capacity == 0) {
					return nullptr;
				}

				if(!_data) {
					_data.reset(new void*[_capacity]());
					_pa_check(std::all_of(_data.get(), _data.get() + _capacity, [](void* ptr) { return ptr == nullptr; }));
				}

				void* const result = _data[_pos];
				_data[_pos] = ptr;
				++_pos;
				if(_pos == _capacity) {
					_pos = 0;
				}

				return result;
			}
		};

		class sized_quarantine_t {
			std::unique_ptr<std::pair<void*, std::size_t>[]> _data;
			std::uint32_t _capacity = 0;
			std::uint32_t _pos = 0;

		public:
			sized_quarantine_t() = default;
			sized_quarantine_t(sized_quarantine_t const& rhs)
			  : _capacity(rhs._capacity)
			  , _pos(rhs._pos) {
				if(_capacity > 0 && rhs._data) {
					_data.reset(new std::pair<void*, std::size_t>[_capacity]);
					for(std::size_t i = 0; i < _capacity; ++i) {
						_data[i] = rhs._data[i];
					}
				}
			}
			sized_quarantine_t& operator=(sized_quarantine_t const& rhs) {
				if(this != &rhs) {
					if(_capacity < rhs._capacity) {
						_data.reset();
					}
					_pos = rhs._pos;
					_capacity = rhs._capacity;
					if(_capacity > 0 && rhs._data) {
						if(!_data) {
							_data.reset(new std::pair<void*, std::size_t>[_capacity]);
						}
						for(std::size_t i = 0; i < _capacity; ++i) {
							_data[i] = rhs._data[i];
						}
					}
				}
				return *this;
			}
			sized_quarantine_t(sized_quarantine_t&&) = default;
			sized_quarantine_t& operator=(sized_quarantine_t&&) = default;

			void initialize(std::uint32_t const capacity) {
				_pa_check(!_data);
				_pa_check(!_capacity);
				_pa_check(!_pos);

				_capacity = capacity;
			}

			std::uint32_t capacity() const noexcept { return _capacity; }

			std::pair<void*, std::size_t> deallocate(void* const ptr, std::size_t const size) {
				if(_capacity == 0) {
					return {nullptr, 0};
				}

				if(!_data) {
					_data.reset(new std::pair<void*, std::size_t>[_capacity]());
					_pa_check(std::all_of(_data.get(), _data.get() + _capacity,
					                      [](auto p) { return p.first == nullptr && p.second == 0; }));
				}

				std::pair<void*, std::size_t> const result = _data[_pos];
				_data[_pos] = {ptr, size};
				++_pos;
				if(_pos == _capacity) {
					_pos = 0;
				}

				return result;
			}
		};
	} // namespace util

	namespace suballocators {
		class sized_heap_t {
			std::vector<std::uint64_t> _bitmap; /// stores the *free* locations as one bits
			std::size_t _finger = 0;

			char* _base = nullptr;
			std::size_t _size = 0;
			std::size_t _slot_size = 0;

			util::quarantine_t _quarantine;

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

			[[nodiscard]] inline std::size_t pos2index(std::size_t const pos) const noexcept {
				int trailing_zeroes = util::ctz(pos);
				auto layer_index = pos >> (trailing_zeroes + 1);
				auto layer = util::ctz(_size) - (trailing_zeroes + 1);
				auto result = (static_cast<std::size_t>(1) << layer) + layer_index - 1;
				return result;
			}

		public:
			void initialize(void* const base, std::size_t const size, std::size_t const slot_size,
			                std::uint32_t const quarantine_size) noexcept {
				_pa_check(size > 0 && (size & (size - 1)) == 0 && "Sizes of sized bins must be powers of two");

				_base = static_cast<char*>(base);
				_size = size;
				_slot_size = slot_size;

				_quarantine.initialize(quarantine_size);
			}

			[[nodiscard]] void* allocate() {
				_pa_check(_finger <= _bitmap.size());
				while(_finger < _bitmap.size() && _bitmap[_finger] == 0) {
					++_finger;
				}
				if(_finger < _bitmap.size()) {
					auto shift = util::ctz(_bitmap[_finger]);
					auto mask = static_cast<::std::uint64_t>(1) << shift;
					_pa_check(mask != 0);
					_pa_check((_bitmap[_finger] & mask) == mask);
					_bitmap[_finger] ^= mask;
					return _base + index2pos(_finger * 64 + shift);
				} else {
					_bitmap.emplace_back(~static_cast<::std::uint64_t>(1));
					return _base + index2pos(_finger * 64 + 0);
				}
			}

			void deallocate(void* ptr) {
				ptr = _quarantine.deallocate(ptr);
				if(!ptr) {
					return;
				}

				auto pos = static_cast<std::size_t>(static_cast<char*>(ptr) - _base);
				_pa_check(pos < _size);
				auto index = pos2index(pos);
				auto loc = index / 64;
				auto shift = index % 64;
				assert(loc < _bitmap.size() && (_bitmap[loc] & (static_cast<std::uint64_t>(1) << shift)) == 0 &&
				       "Invalid free");
				if(loc < _finger) {
					_finger = loc;
				}

				auto mask = static_cast<std::uint64_t>(1) << shift;
				_pa_check(mask != 0);
				_pa_check((_bitmap[loc] & mask) == 0);
				_bitmap[loc] |= mask;
				if(loc + 1 == _bitmap.size()) {
					while(!_bitmap.empty() && _bitmap.back() == ~static_cast<::std::uint64_t>(0)) {
						_bitmap.pop_back();
					}
					_finger = _bitmap.size();
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
			/// tie-breaker between those. To this end, we use a vector that effectively uses insertion order as a
			/// tie-breaker.
			std::map<std::size_t, std::vector<char*>, std::greater<std::size_t>> map1;

			/// The second direction of the mapping, `map2`, maps the position of each free region to its size. Its ordering
			/// is ascending, so that a lookup for the position of an allocated object is able to return the previous and the
			/// following unallocated region.
			std::map<char*, std::size_t> map2;

			util::sized_quarantine_t _quarantine;

		public:
			void initialize(void* base, std::size_t size, std::uint32_t const quarantine_size) {
				map1.emplace(size, std::initializer_list<char*>{static_cast<char*>(base)});
				map2.emplace(static_cast<char*>(base), size);

				_quarantine.initialize(quarantine_size);

#if defined(PSEUDOALLOC_TRACE)
				::std::cout << "[LOH] Initialization complete.\n";
				trace();
#endif
			}

			[[nodiscard]] void* allocate(std::size_t size) {
#if defined(PSEUDOALLOC_TRACE)
				::std::cout << "[LOH] Allocating " << size << " (" << util::round_up_to_multiple_of_4096(size) << ") bytes\n";
				trace();
#endif

				_pa_check(size > 4096);
				size = util::round_up_to_multiple_of_4096(size);

				_pa_check(!map1.empty());
				auto largest_free_range_it = map1.begin();
				std::size_t const range_size = largest_free_range_it->first;
				assert(range_size + 2 * 4096 >= size && "Zero (or below) red zone size!");
				char* const range_pos = largest_free_range_it->second.back();
				largest_free_range_it->second.pop_back();
				decltype(map1)::node_type node;
				if(largest_free_range_it->second.empty()) {
					node = map1.extract(largest_free_range_it);
				}

				auto offset = (range_size - size) / 2;
				offset = util::round_up_to_multiple_of_4096(offset);
				auto const left_size = offset;
				auto const left_pos = range_pos;
				auto const right_size = range_size - offset - size;
				auto const right_pos = range_pos + offset + size;

				// left subrange
				{
					decltype(map1)::iterator it1;
					if(!node.empty()) {
						node.key() = left_size;
						_pa_check(node.mapped().empty());
						it1 = map1.insert(std::move(node)).position;
					} else {
						it1 = std::get<0>(map1.try_emplace(left_size));
					}
					auto& v1 = it1->second;
					v1.emplace_back(left_pos);
					if(left_size == right_size) {
						v1.emplace_back(right_pos);
					}
				}
				auto finger = map2.find(range_pos);
				_pa_check(finger != map2.end());
				_pa_check(finger->second == range_size);
				_pa_check(finger->first == left_pos);
				finger->second = left_size;

				// right subrange
				if(left_size != right_size) {
					auto it1 = std::get<0>(map1.try_emplace(right_size));
					it1->second.emplace_back(right_pos);
				}
				map2.emplace_hint(std::next(finger), right_pos, right_size);

				return range_pos + offset;
			}

			void deallocate(void* ptr, std::size_t size) {
#if defined(PSEUDOALLOC_TRACE)
				::std::cout << "[LOH] Qurantining " << ptr << " with size " << size << " ("
				            << util::round_up_to_multiple_of_4096(size) << ")\n";
				trace();
#endif

				{
					auto pair = _quarantine.deallocate(ptr, size);
					ptr = pair.first;
					size = pair.second;
					if(!ptr) {
						return;
					}
				}

#if defined(PSEUDOALLOC_TRACE)
				::std::cout << "[LOH] Freeing " << ptr << " with size " << size << " ("
				            << util::round_up_to_multiple_of_4096(size) << ")\n";
				trace();
#endif

				_pa_check(size > 4096);
				size = util::round_up_to_multiple_of_4096(size);

				auto right_it = map2.upper_bound(static_cast<char*>(ptr));
				auto left_it = std::prev(right_it);
				auto const left_pos = left_it->first;
				auto const left_size = left_it->second;
				auto const right_pos = right_it->first;
				auto const right_size = right_it->second;
				_pa_check(left_pos + left_size == static_cast<char*>(ptr));
				_pa_check(left_pos + left_size + size == right_pos);

				decltype(map1)::node_type node;

				{
					auto it1 = map1.find(left_size);
					_pa_check(it1 != map1.end());
					if(left_size == right_size) {
						_pa_check(it1->second.size() >= 2);
						auto it1v = std::find_if(it1->second.begin(), it1->second.end(),
						                         [left_pos, right_pos](char* ptr) { return ptr == left_pos || ptr == right_pos; });
						_pa_check(it1v != it1->second.end());
						if(*it1v == left_pos) {
							*it1v = it1->second.back();
							it1v = std::find(it1v, std::prev(it1->second.end()), right_pos);
						} else {
							_pa_check(*it1v == right_pos);
							*it1v = it1->second.back();
							it1v = std::find(it1v, std::prev(it1->second.end()), left_pos);
						}
						_pa_check(it1v < std::prev(it1->second.end()));
						*it1v = it1->second[it1->second.size() - 2];
						it1->second.pop_back();
						it1->second.pop_back();
						if(it1->second.empty()) {
							node = map1.extract(it1);
						}
					} else {
						auto it1v = std::find(it1->second.begin(), it1->second.end(), left_pos);
						_pa_check(it1v != it1->second.end());
						*it1v = it1->second.back();
						it1->second.pop_back();
						if(it1->second.empty()) {
							node = map1.extract(it1);
						}

						it1 = map1.find(right_size);
						_pa_check(it1 != map1.end());
						it1v = std::find(it1->second.begin(), it1->second.end(), right_pos);
						_pa_check(it1v != it1->second.end());
						*it1v = it1->second.back();
						it1->second.pop_back();
						if(it1->second.empty()) {
							node = map1.extract(it1);
						}
					}
				}

				std::size_t const combined_size = left_size + size + right_size;
				if(!node.empty()) {
					node.key() = combined_size;
					_pa_check(node.mapped().empty());
					auto it1 = map1.insert(::std::move(node)).position;
					it1->second.emplace_back(left_pos);
				} else {
					auto it1 = std::get<0>(map1.try_emplace(combined_size));
					it1->second.emplace_back(left_it->first);
				}

				left_it->second = combined_size;
				map2.erase(right_it);
			}

#if defined(PSEUDOALLOC_TRACE)
			void trace() {
				::std::cout << "[LOH] map1:\n";
				for(auto const& x : map1) {
					::std::cout << "      " << x.first << "\n";
					for(auto const& y : x.second) {
						::std::cout << "        " << static_cast<void*>(y) << "\n";
					}
				}
				::std::cout << "[LOH] map2:\n";
				for(auto const& x : map2) {
					::std::cout << "      " << static_cast<void*>(x.first) << " " << x.second << "\n";
				}
			}
#endif
		};
	} // namespace suballocators

	/// Wraps a mapping that is shared with other allocators that are required to return
	/// identical addresses. The bins are maintained in the following layout in the mapping:
	///
	/// +---------------------------------------------------+
	/// | 4 | 8 | 16 | ... | 2048 | 4096 | large object bin |
	/// +---------------------------------------------------+
	class allocator_t {
		mapping_t* _mapping;
		std::array<suballocators::sized_heap_t, 11> _sized_bins;
		suballocators::large_object_heap_t _loh;

		[[nodiscard]] static inline int size2bin(std::size_t size) noexcept {
			if(size <= 4) {
				return 0;
			} else if(size > 4096) {
				return 11;
			} else {
				int result = (std::numeric_limits<std::size_t>::digits - 2) - util::clz(size - 1);
				_pa_check(result > 0 && result < 11);
				return result;
			}
		}

	public:
		allocator_t(mapping_t& mapping, std::uint32_t const quarantine_size)
		  : _mapping(&mapping) {
			assert(mapping.size() > (_sized_bins.size() + 1) && "Mapping is *far* to small");

			auto bin_size = static_cast<std::size_t>(1) << (std::numeric_limits<std::size_t>::digits - 1 -
			                                                util::clz(_mapping->size() / (_sized_bins.size() + 1)));
			char* const base = static_cast<char*>(_mapping->begin());
			std::size_t slot_size = 8;
			std::size_t total_size = 0;
			for(auto& bin : _sized_bins) {
				bin.initialize(base + total_size, bin_size, slot_size, quarantine_size);

				total_size += bin_size;
				assert(total_size <= _mapping->size() && "Mapping too small");
				slot_size *= 2;
			}

			auto loh_size = mapping.size() - total_size;
			assert(loh_size > 0);
			_loh.initialize(base + total_size, loh_size, quarantine_size);
		}

		allocator_t(allocator_t const&) = default;
		allocator_t& operator=(allocator_t const&) = default;
		allocator_t(allocator_t&&) = default;
		allocator_t& operator=(allocator_t&&) = default;

		[[nodiscard]] void* allocate(std::size_t size) {
			if(auto bin = size2bin(size); bin < static_cast<int>(_sized_bins.size())) {
				return _sized_bins[bin].allocate();
			} else {
				return _loh.allocate(size);
			}
		}

		void free(void* ptr, std::size_t size) {
			assert(ptr && "Freeing nullptrs is not supported"); // we are not ::free!

			if(auto bin = size2bin(size); bin < static_cast<int>(_sized_bins.size())) {
				return _sized_bins[bin].deallocate(ptr);
			} else {
				return _loh.deallocate(ptr, size);
			}
		}
	};

	using stack_allocator_t = allocator_t;
} // namespace pseudoalloc
