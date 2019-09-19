#pragma once

#include "por/thread_id.h"

#include <util/iterator_range.h>

#include <map>
#include <memory>

namespace por::event {
	class event;
}

namespace por {
	class cone {
		std::map<thread_id, por::event::event const*> _map;

		void insert(por::event::event const& event);

	public:
		auto begin() const { return _map.begin(); }
		auto end() const { return _map.end(); }
		auto size() const { return _map.size(); }
		auto empty() const { return _map.empty(); }
		auto find(thread_id const& tid) const { return _map.find(tid); }
		auto at(thread_id const& tid) const { return _map.at(tid); }
		auto count(thread_id const& tid) const { return _map.count(tid); }

		cone() = default;

		cone(por::event::event const& immediate_predecessor);

		cone(por::event::event const* immediate_predecessor,
			por::event::event const* single_other_predecessor,
			util::iterator_range<por::event::event const**> other_predecessors);

		[[deprecated]]
		cone(por::event::event const& immediate_predecessor,
			por::event::event const* single_other_predecessor,
			util::iterator_range<std::shared_ptr<por::event::event>*> other_predecessors);

	};
}
