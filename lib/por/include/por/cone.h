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

		// IMPORTANT: assumes no conflict between this and rhs
		bool is_lte_for_all_of(cone const& rhs) const noexcept;

		// IMPORTANT: assumes no conflict between this and rhs
		bool is_gte_for_all_of(cone const& rhs) const noexcept;

		void extend_unchecked_single(por::event::event const& event) noexcept;
	};
}
