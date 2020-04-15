#pragma once

#include <cstddef>

namespace por::event {
	class event;
}

namespace por {
	using csd_t = std::size_t;

	bool is_above_csd_limit(por::event::event const& local_configuration, csd_t limit);

	csd_t compute_csd(por::event::event const& local_configuration);
}
