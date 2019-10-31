#pragma once

#include "por/event/event.h"

#include <cstddef>

namespace por {
	bool is_above_csd_limit(por::event::event const& local_configuration, std::size_t limit) {
		return false;
	}
}
