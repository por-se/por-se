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

		template<decltype(nullptr)>
		std::size_t size() const noexcept {
			return 0;
		}

		std::size_t size() const noexcept {
			return std::distance(_begin, _end);
		}
	};

	template<typename T>
	auto make_iterator_range(T begin, T end) noexcept(std::is_nothrow_constructible_v<std::decay_t<T>, T&&>) {
		return iterator_range<std::decay_t<T>>(std::move(begin), std::move(end));
	}
}
