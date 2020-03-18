#pragma once

#include "util/create_uninitialized.h"

#include <cstddef>
#include <cstdlib>
#include <type_traits>
#include <algorithm>

namespace util {
	namespace _sso_array_bits {
		template<typename T, std::size_t sso_capacity> struct size       : std::integral_constant<std::size_t, sizeof(T[sso_capacity])> { };
		template<typename T>                           struct size<T, 0> : std::integral_constant<std::size_t, 0> { };
		template<typename T, std::size_t sso_capacity> struct align       : std::integral_constant<std::size_t, alignof(T[sso_capacity])> { };
		template<typename T>                           struct align<T, 0> : std::integral_constant<std::size_t, 0> { };

		template<typename T, std::size_t sso_capacity>
		using data_t = std::aligned_storage_t<
			std::max(sizeof(T*), _sso_array_bits::size<T, sso_capacity>::value),
			std::max(alignof(T*), _sso_array_bits::align<T, sso_capacity>::value)
		>;
	}

	template<typename T, std::size_t sso_capacity>
	class sso_array {
		using data_t = _sso_array_bits::data_t<T, sso_capacity>;

		std::size_t _size = 0;
		data_t _data;

	public:
		sso_array() noexcept = default;

		// WARNING when using this ctor directly:
		// All elements *MUST* be initialized before the sso_array is destructed!
		sso_array(create_uninitialized_t, std::size_t const size) noexcept
			: _size(size)
		{
			assert((_size * sizeof(T)) / sizeof(T) == _size && "overflow in size computation");
			if constexpr(sso_capacity == 0) {
				if(_size > 0) {
					reinterpret_cast<T*&>(_data) = static_cast<T*>(std::malloc(_size * sizeof(T)));
					assert(reinterpret_cast<T*&>(_data) && "malloc failed");
				}
			} else if(_size > sso_capacity) {
				reinterpret_cast<T*&>(_data) = static_cast<T*>(std::malloc(_size * sizeof(T)));
				assert(reinterpret_cast<T*&>(_data) && "malloc failed");
			}
		}

		sso_array(std::size_t const size) noexcept(std::is_nothrow_default_constructible_v<T>)
			: sso_array(create_uninitialized, size)
		{
			if constexpr(sso_capacity == 0) {
				for(std::size_t i = 0; i < _size; ++i) {
					new(reinterpret_cast<T*&>(_data) + i) T();
				}
			} else if(_size > sso_capacity) {
				for(std::size_t i = 0; i < _size; ++i) {
					new(reinterpret_cast<T*&>(_data) + i) T();
				}
			} else {
				for(std::size_t i = 0; i < _size; ++i) {
					new(reinterpret_cast<T(&)[sso_capacity]>(_data) + i) T();
				}
			}
		}

		sso_array(sso_array const& rhs) noexcept(std::is_nothrow_copy_constructible_v<T>)
			: sso_array(create_uninitialized, rhs._size)
		{
			if constexpr(sso_capacity == 0) {
				for(std::size_t i = 0; i < _size; ++i) {
					new(reinterpret_cast<T*&>(_data) + i) T(rhs[i]);
				}
			} else if(_size > sso_capacity) {
				for(std::size_t i = 0; i < _size; ++i) {
					new(reinterpret_cast<T*&>(_data) + i) T(rhs[i]);
				}
			} else {
				for(std::size_t i = 0; i < _size; ++i) {
					new(reinterpret_cast<T(&)[sso_capacity]>(_data) + i) T(rhs[i]);
				}
			}
		}
		sso_array& operator=(sso_array const&) = delete;

		sso_array(sso_array&& rhs) noexcept(std::is_nothrow_move_constructible_v<T>)
			: _size(rhs._size)
		{
			assert((_size * sizeof(T)) / sizeof(T) == _size && "overflow in size computation");
			if constexpr(sso_capacity == 0) {
				reinterpret_cast<T*&>(_data) = reinterpret_cast<T*&>(rhs._data);
				rhs._size = 0;
				#if DEBUG
					reinterpret_cast<T*&>(rhs._data) = nullptr;
				#endif
			} else if(_size > sso_capacity) {
				reinterpret_cast<T*&>(_data) = reinterpret_cast<T*&>(rhs._data);
				rhs._size = 0;
				#if DEBUG
					reinterpret_cast<T*&>(rhs._data) = nullptr;
				#endif
			} else {
				auto& lhs_data = reinterpret_cast<T(&)[sso_capacity]>(_data);
				auto& rhs_data = reinterpret_cast<T(&)[sso_capacity]>(rhs._data);
				for(std::size_t i = 0; i < _size; ++i) {
					new(lhs_data + i) T(std::move(rhs_data[i]));
				}
				rhs._size = 0;
			}
		}
		sso_array& operator=(sso_array&&) = delete;

		~sso_array() {
			if constexpr(sso_capacity == 0) {
				if(_size > 0) {
					for(std::size_t i = 0; i < _size; ++i) {
						reinterpret_cast<T*&>(_data)[i].~T();
					}
					std::free(reinterpret_cast<T*&>(_data));
				}
			} else if(_size > sso_capacity) {
					for(std::size_t i = 0; i < _size; ++i) {
						reinterpret_cast<T*&>(_data)[i].~T();
					}
					std::free(reinterpret_cast<T*&>(_data));
			} else {
				for(std::size_t i = 0; i < _size; ++i) {
					reinterpret_cast<T(&)[sso_capacity]>(_data)[i].~T();
				}
			}
		}

		T* data() noexcept {
			if constexpr(sso_capacity == 0) {
				return reinterpret_cast<T*&>(_data);
			} else if(_size > sso_capacity) {
				return reinterpret_cast<T*&>(_data);
			} else {
				return reinterpret_cast<T(&)[sso_capacity]>(_data);
			}
		}
		T const* data() const noexcept {
			if constexpr(sso_capacity == 0) {
				return reinterpret_cast<T const* const&>(_data);
			} else if(_size > sso_capacity) {
				return reinterpret_cast<T const* const&>(_data);
			} else {
				return reinterpret_cast<T const(&)[sso_capacity]>(_data);
			}
		}

		T      & operator[](std::size_t const index)       noexcept { return data()[index]; }
		T const& operator[](std::size_t const index) const noexcept { return data()[index]; }

		std::size_t size() const noexcept { return _size; }
		bool empty() const noexcept { return _size == 0; }
	};
}
