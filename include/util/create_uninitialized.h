#pragma once

namespace util {
	struct create_uninitialized_t {
		explicit constexpr create_uninitialized_t(int) { }
	};

	inline constexpr create_uninitialized_t create_uninitialized{0};
}
