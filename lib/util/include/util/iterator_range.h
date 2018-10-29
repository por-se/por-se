#pragma once

#include <type_traits>
#include <utility>

namespace util {
	template<typename T>
	class iterator_range {
		std::decay_t<T> _begin, _end;

	public:
		iterator_range(T begin, T end)
		: _begin(begin)
		, _end(end)
		{ }

		std::decay_t<T> const& begin() noexcept {
			return _begin;
		}

		std::decay_t<T> const& end() noexcept {
			return _end;
		}
	};

	template<typename T>
	auto make_iterator_range(T begin, T end) {
		return iterator_range<std::decay_t<T>>(std::move(begin), std::move(end));
	}
}
