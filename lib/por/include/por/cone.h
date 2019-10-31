#pragma once

#include "por/thread_id.h"

#include <util/iterator_range.h>

#include <map>

namespace por::event {
	class event;
}

namespace por {
	class cone {
		std::map<thread_id, por::event::event const*> _map;

	public:
		auto begin() const { return _map.begin(); }
		auto end() const { return _map.end(); }
		auto size() const { return _map.size(); }
		auto empty() const { return _map.empty(); }
		auto find(thread_id const& tid) const { return _map.find(tid); }
		auto at(thread_id const& tid) const { return _map.at(tid); }
		auto count(thread_id const& tid) const { return _map.count(tid); }

		void insert(por::event::event const& event);

		cone() = default;

		cone(por::event::event const& immediate_predecessor);

		cone(util::iterator_range<por::event::event const* const*> events);

		cone(por::event::event const& immediate_predecessor,
			por::event::event const* single_other_predecessor,
			util::iterator_range<por::event::event const* const*> other_predecessors);
	};
}
