#pragma once

#include <cstddef>

namespace por::event {
	class event;
}

namespace por {
	bool compare_adequate_total_order(por::event::event const& a, por::event::event const& b);
}
