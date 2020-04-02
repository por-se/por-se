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
#include <ostream>
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

#ifndef PSEUDOALLOC_TRACE
#	define PSEUDOALLOC_TRACE 0
#endif

#if PSEUDOALLOC_TRACE >= 1
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
		mapping_t() = default;

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
		mapping_t(mapping_t&& other)
		  : _begin(other._begin)
		  , _size(other._size) {
			other._begin = MAP_FAILED;
			other._size = 0;
		}
		mapping_t& operator=(mapping_t&& other) {
			if(&other != this) {
				using std::swap;
				swap(other._begin, _begin);
				swap(other._size, _size);
			}
			return *this;
		}

		[[nodiscard]] void* begin() const noexcept {
			assert(*this && "Invalid mapping");
			return _begin;
		}

		[[nodiscard]] std::size_t size() const noexcept { return _size; }

		void clear() {
			assert(*this && "Invalid mapping");
			[[maybe_unused]] int rc = ::madvise(_begin, _size, MADV_DONTNEED);
			assert(rc == 0 && "madvise failed");
		}

		explicit operator bool() const noexcept { return _begin != MAP_FAILED; }

		~mapping_t() {
			if(*this) {
				[[maybe_unused]] int rc = ::munmap(_begin, _size);
				assert(rc == 0 && "munmap failed");
			}
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

		template <typename C> class tagged_logger_t {
		protected:
			template <typename... T> inline void traceln(T&&... args) const noexcept {
#if PSEUDOALLOC_TRACE >= 1
				(static_cast<C const*>(this)->log_tag(std::cout) << ... << std::forward<T>(args)) << "\n";
#else
				(static_cast<void>(std::forward<T>(args)), ..., static_cast<void>(0)); // ignore all arguments
#endif
			}
		};

		struct quarantine_base_t {
			static const std::uint32_t unlimited = static_cast<std::uint32_t>(-1);
		};

		template <typename T> class quarantine_t : public quarantine_base_t {
			std::unique_ptr<T[]> _data;
			std::uint32_t _capacity = 0;
			std::uint32_t _pos = 0;

		public:
			quarantine_t() = default;
			quarantine_t(quarantine_t const& rhs)
			  : _capacity(rhs._capacity)
			  , _pos(rhs._pos) {
				if(rhs._data) {
					_pa_check(_capacity > 0);
					_pa_check(_capacity != unlimited);
					_data.reset(new T[_capacity]);
					for(std::size_t i = 0; i < _capacity; ++i) {
						_data[i] = rhs._data[i];
					}
				}
			}
			quarantine_t& operator=(quarantine_t const& rhs) {
				if(this != &rhs) {
					if(rhs._data) {
						_pa_check(rhs._capacity > 0);
						_pa_check(rhs._capacity != unlimited);
						if(_capacity != rhs._capacity) {
							_capacity = rhs._capacity;
							_data.reset(new T[_capacity]);
						}
						_pos = rhs._pos;
						for(std::size_t i = 0; i < _capacity; ++i) {
							_data[i] = rhs._data[i];
						}
					} else {
						_capacity = rhs._capacity;
						_pos = rhs._pos;
						_data.reset();
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

			constexpr std::uint32_t capacity() const noexcept { return _capacity; }

			T deallocate(T const ptr) {
				if(_capacity == 0) {
					return ptr;
				}
				if(_capacity == unlimited) {
					return T{};
				}

				if(!_data) {
					_pa_check(_pos == 0);
					_data.reset(new T[_capacity]());
					_pa_check(std::all_of(_data.get(), _data.get() + _capacity, [](T ptr) { return ptr == T{}; }));
				}

				auto const result = std::exchange(_data[_pos], std::move(ptr));
				++_pos;
				if(_pos == _capacity) {
					_pos = 0;
				}

				return result;
			}

			template <typename F> bool any_of(F predicate) const {
				if(!_data) {
					return false;
				}
				return std::any_of(_data.get(), _data.get() + _capacity, predicate);
			}
		};
	} // namespace util

	namespace suballocators {
		class sized_heap_t : public util::tagged_logger_t<sized_heap_t> {
			std::vector<std::uint64_t> _bitmap; /// stores the *free* locations as one bits
			std::size_t _finger = 0;

			char* _base = nullptr;
			std::size_t _size = 0;
			std::size_t _slot_size = 0;

			util::quarantine_t<void*> _quarantine;

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
			inline std::ostream& log_tag(std::ostream& out) const noexcept { return out << "[" << _slot_size << "] "; }

			void initialize(void* const base, std::size_t const size, std::size_t const slot_size,
			                std::uint32_t const quarantine_size) noexcept {
				_pa_check(size > 0 && (size & (size - 1)) == 0 && "Sizes of sized bins must be powers of two");

				_base = static_cast<char*>(base);
				_size = size;
				_slot_size = slot_size;

				_quarantine.initialize(quarantine_size);

				traceln("Initialization complete");
			}

			[[nodiscard]] void* allocate() {
				traceln("Allocating ", _slot_size, " bytes");
				trace_contents();

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
					_pa_check(_finger == _bitmap.size());
					_bitmap.emplace_back(~static_cast<::std::uint64_t>(1));
					return _base + index2pos(_finger * 64 + 0);
				}
			}

			[[nodiscard]] bool may_deallocate(void* ptr) const {
				_pa_check(ptr >= _base && ptr < _base + _size);
				auto const pos = static_cast<std::size_t>(static_cast<char*>(ptr) - _base);
				_pa_check(pos < _size);

				auto const index = pos2index(pos);
				auto const loc = index / 64;
				auto const shift = index % 64;
				if(_quarantine.any_of([ptr](void* entry) { return ptr == entry; })) {
					return false;
				}
				return loc < _bitmap.size() && (_bitmap[loc] & (static_cast<std::uint64_t>(1) << shift)) == 0;
			}

			void deallocate(void* ptr) {
				traceln("Quarantining ", ptr, " for ", _quarantine.capacity(), " deallocations");
				_pa_check(may_deallocate(ptr) && "Invalid free");
				ptr = _quarantine.deallocate(ptr);

				traceln("Deallocating ", ptr);
				if(!ptr) {
					return;
				}
				trace_contents();

				auto const pos = static_cast<std::size_t>(static_cast<char*>(ptr) - _base);
				_pa_check(pos < _size);

				auto const index = pos2index(pos);
				auto const loc = index / 64;
				auto const shift = index % 64;
				assert(loc < _bitmap.size() && (_bitmap[loc] & (static_cast<std::uint64_t>(1) << shift)) == 0 &&
				       "Invalid free. (Possibly delayed due to quarantine. Enable expensive checks with PSEUDOALLOC_CHECKED to "
				       "detect invalid frees immediately.)");
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
				}
			}

			void trace_contents() const noexcept {
				if(_bitmap.empty()) {
					traceln("bitmap is empty");
				} else {
#if PSEUDOALLOC_TRACE >= 2
					traceln("bitmap:");
					for(std::size_t i = 0; i < _bitmap.size(); ++i) {
						auto mask = static_cast<std::uint64_t>(1);
						if(~_bitmap[i] != 0) {
							for(std::size_t j = 0; j < 64; ++j) {
								if((_bitmap[i] & (static_cast<std::uint64_t>(1) << j)) == 0) {
									traceln("  ", i * 64 + j, " ", static_cast<void*>(_base + index2pos(i * 64 + j)));
								}
							}
						}
					}
#else
					traceln("bitmap contains ", _bitmap.size(), " elements (64-bit words)");
#endif
				}
			}
		};

		/// The large object heap is implemented as what amounts to as a bi-directional mapping between the position of
		/// each unallocated region and its actual size. The implemented algorithm performs allocations in the middle of
		/// the largest available unallocated region. Allocations are guaranteed to be aligned to 4096 bytes.
		class large_object_heap_t : public util::tagged_logger_t<large_object_heap_t> {
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

			util::quarantine_t<std::pair<void*, std::size_t>> _quarantine;

		public:
			inline std::ostream& log_tag(std::ostream& out) const noexcept { return out << "[LOH] "; }

			void initialize(void* base, std::size_t size, std::uint32_t const quarantine_size) {
				map1.emplace(size, std::initializer_list<char*>{static_cast<char*>(base)});
				map2.emplace(static_cast<char*>(base), size);

				_quarantine.initialize(quarantine_size);

				traceln("Initialization complete");
				trace_contents();
			}

			[[nodiscard]] void* allocate(std::size_t size) {
				auto const quantized_size = util::round_up_to_multiple_of_4096(size);
				traceln("Allocating ", size, " (", quantized_size, ") bytes");
				_pa_check(size > 4096);
				trace_contents();

				size = quantized_size;

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

			[[nodiscard]] bool may_deallocate(void* ptr, std::size_t size) {
				{
					auto last = map2.cend();
					--last;
					_pa_check(ptr >= map2.cbegin()->first && ptr < last->first + last->second);
				}
				_pa_check(size > 4096);

				return true; // FIXME: not yet implemented!
			}

			void deallocate(void* ptr, std::size_t size) {
				auto quantized_size = util::round_up_to_multiple_of_4096(size);
				traceln("Quarantining ", ptr, " with size ", size, " (", quantized_size, ") for ", _quarantine.capacity(),
				        " deallocations");
				_pa_check(may_deallocate(ptr, size) && "Invalid free");

				std::tie(ptr, size) = _quarantine.deallocate(std::make_pair(ptr, size));
				quantized_size = util::round_up_to_multiple_of_4096(size);
				traceln("Deallocating ", ptr, " with size ", size, " (", quantized_size, ")");

				if(!ptr) {
					return;
				}

				_pa_check(size > 4096);
				size = quantized_size;
				trace_contents();

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

			void trace_contents() const noexcept {
				if(map1.empty()) {
					traceln("map1 is empty");
				} else {
#if PSEUDOALLOC_TRACE >= 2
					traceln("map1:");
					for(auto const& x : map1) {
						traceln("  ", x.first);
						for(auto const& y : x.second) {
							traceln("    ", static_cast<void*>(y));
						}
					}
#else
					traceln("map1 has ", map1.size(), " entries");
#endif
				}

				if(map2.empty()) {
					traceln("map2 is empty");
				} else {
#if PSEUDOALLOC_TRACE >= 2
					traceln("map2:");
					for(auto const& x : map2) {
						traceln("  ", static_cast<void*>(x.first), x.second);
					}
#else
					traceln("map2 has ", map2.size(), " entries");
#endif
				}
			}
		};
	} // namespace suballocators

	/// Wraps a mapping that is shared with other allocators.
	class allocator_t : public util::tagged_logger_t<allocator_t> {
		static constexpr const std::array _meta = {
		  1u,    // bool
		  4u,    // int
		  8u,    // pointer size
		  16u,   // double
		  32u,   // compound types #1
		  64u,   // compound types #2
		  256u,  // compound types #3
		  1024u, // compound types #4
		  4096u  // the LOH can only manage objects larger than 4096 bytes
		};

	public:
		static const std::uint32_t unlimited_quarantine = util::quarantine_base_t::unlimited;

	private:
		mapping_t* _mapping = nullptr;
		std::array<suballocators::sized_heap_t, _meta.size()> _sized_bins;
		suballocators::large_object_heap_t _loh;

		[[nodiscard]] static inline int size2bin(std::size_t const size) noexcept {
			for(std::size_t i = 0; i < _meta.size(); ++i) {
				if(_meta[i] >= size) {
					return i;
				}
			}
			return _meta.size();
		}

	public:
		std::ostream& log_tag(std::ostream& out) const noexcept { return out << "[alloc] "; }

		allocator_t() = default;

		allocator_t(mapping_t& mapping, std::uint32_t const quarantine_size)
		  : _mapping(&mapping) {
			assert(mapping && "Invalid mapping");
			assert(mapping.size() > _meta.size() + 1 && "Mapping is *far* to small");

			auto bin_size = static_cast<std::size_t>(1) << (std::numeric_limits<std::size_t>::digits - 1 -
			                                                util::clz(_mapping->size() / (_meta.size() + 1)));
			char* const base = static_cast<char*>(_mapping->begin());
			std::size_t total_size = 0;
			for(std::size_t i = 0; i < _meta.size(); ++i) {
				static_assert(_meta.size() == std::tuple_size_v<decltype(_sized_bins)>);

				_sized_bins[i].initialize(base + total_size, bin_size, _meta[i], quarantine_size);

				total_size += bin_size;
				assert(total_size <= _mapping->size() && "Mapping too small");
			}

			auto loh_size = mapping.size() - total_size;
			assert(loh_size > 0);
			_loh.initialize(base + total_size, loh_size, quarantine_size);
		}

		allocator_t(allocator_t const&) = default;
		allocator_t& operator=(allocator_t const&) = default;
		allocator_t(allocator_t&&) = default;
		allocator_t& operator=(allocator_t&&) = default;

		explicit operator bool() const noexcept { return _mapping != nullptr; }

		[[nodiscard]] void* allocate(std::size_t size) {
			assert(*this && "Invalid allocator");

			auto const bin = size2bin(size);
			traceln("Allocating ", size, " bytes in bin ", bin);

			void* result = nullptr;
			if(bin < static_cast<int>(_sized_bins.size())) {
				result = _sized_bins[bin].allocate();
			} else {
				result = _loh.allocate(size);
			}
			traceln("Allocated ", result);
			return result;
		}

		void free(void* ptr, std::size_t size) {
			assert(*this && "Invalid allocator");
			assert(ptr && "Freeing nullptrs is not supported"); // we are not ::free!

			auto const bin = size2bin(size);
			traceln("Freeing ", ptr, " of size ", size, " in bin ", bin);

			if(bin < static_cast<int>(_sized_bins.size())) {
				return _sized_bins[bin].deallocate(ptr);
			} else {
				return _loh.deallocate(ptr, size);
			}
		}
	};

	using stack_allocator_t = allocator_t;
} // namespace pseudoalloc
