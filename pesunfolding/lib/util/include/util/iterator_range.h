#pragma once

#include <type_traits>
#include <utility>

namespace util {
	template<typename T>
	class iterator_range {
		std::decay_t<T> _begin, _end;

	public:
		iterator_range(T begin, T end) noexcept(std::is_nothrow_constructible_v<std::decay_t<T>, T&&>)
		: _begin(std::move(begin))
		, _end(std::move(end))
		{ }

		std::decay_t<T> const& begin() noexcept {
			return _begin;
		}

		std::decay_t<T> const& end() noexcept {
			return _end;
		}

		std::size_t size() const noexcept {
			if constexpr(std::is_same_v<std::decay_t<T>, std::decay_t<decltype(nullptr)>>) {
				return 0;
			} else {
				return std::distance(_begin, _end);
			}
		}

		bool empty() const noexcept {
			return _begin == _end;
		}
	};

	template<typename T>
	auto make_iterator_range(T begin, T end) noexcept(std::is_nothrow_constructible_v<std::decay_t<T>, T&&>) {
		return iterator_range<std::decay_t<T>>(std::move(begin), std::move(end));
	}
}
