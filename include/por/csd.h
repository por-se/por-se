#pragma once

#include <cstddef>

namespace por::event {
	class event;
}

namespace por {
	using csd_t = std::size_t;

	bool is_above_csd_limit_1(por::event::event const& local_configuration, csd_t limit);
	bool is_above_csd_limit_2(por::event::event const& local_configuration, csd_t limit);

	csd_t compute_csd_1(por::event::event const& local_configuration);
	csd_t compute_csd_2(por::event::event const& local_configuration);

	template<typename... V>
	inline auto is_above_csd_limit(V&&... args) {
		return is_above_csd_limit_2(std::forward<V>(args)...);
	}
}
