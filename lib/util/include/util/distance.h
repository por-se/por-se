#pragma once

#include <iterator>

namespace util {
	template<typename T>
	constexpr std::size_t distance(T&& a, T&& b) {
		return std::distance(a, b);
	}

	constexpr std::size_t distance(decltype(nullptr), decltype(nullptr)) {
		return 0;
	}
}
