#pragma once

#include "por/comb.h"
#include "por/thread_id.h"

#include "util/check.h"
#include "util/iterator_range.h"

#include <map>

namespace por::event {
	class event;
}

namespace por {
	class cone;
	class configuration;

	class cone_event_iterator {
		por::cone const* _cone = nullptr;
		std::map<thread_id, por::event::event const*>::const_iterator _event;

	public:
		using value_type = por::event::event const*;
		using difference_type = std::ptrdiff_t;
		using pointer = por::event::event const* const*;
		using reference = por::event::event const* const&;

		using iterator_category = std::forward_iterator_tag;

		cone_event_iterator() = default;
		explicit cone_event_iterator(por::cone const& cone, bool end=false);

		reference operator*() const noexcept { return _event->second; }
		pointer operator->() const noexcept { return &_event->second; }

		cone_event_iterator& operator++() noexcept;
		cone_event_iterator operator++(int) noexcept {
			cone_event_iterator tmp = *this;
			++(*this);
			return tmp;
		}

		bool operator==(const cone_event_iterator& rhs) const noexcept {
			libpor_check(decltype(_event)() == decltype(_event)());
			return _cone == rhs._cone && _event == rhs._event;
		}
		bool operator!=(const cone_event_iterator& rhs) const noexcept {
			return !(*this == rhs);
		}
	};

	// includes maximal event per thread (excl. program_init / thread 0)
	class cone {
		std::map<thread_id, por::event::event const*> _map;

	public:
		using iterator = decltype(_map)::const_iterator;
		using const_iterator = iterator;
		using reverse_iterator = decltype(_map)::const_reverse_iterator;
		using const_reverse_iterator = reverse_iterator;

		iterator begin() const { return _map.cbegin(); }
		iterator end() const { return _map.cend(); }
		reverse_iterator rbegin() const { return _map.crbegin(); }
		reverse_iterator rend() const { return _map.crend(); }
		auto size() const { return _map.size(); }
		auto empty() const { return _map.empty(); }
		auto find(thread_id const& tid) const { return _map.find(tid); }
		auto at(thread_id const& tid) const { return _map.at(tid); }
		auto has(thread_id const& tid) const { return _map.count(tid) != 0; }

		auto events_begin() const noexcept { return cone_event_iterator(*this); }
		auto events_end() const noexcept { return cone_event_iterator(*this, true); }

		auto events() const noexcept { return util::make_iterator_range(events_begin(), events_end()); }

		void insert(por::event::event const& event);

		cone() = default;

		cone(por::event::event const& immediate_predecessor);

		template<typename T>
		cone(T begin, T end) {
			for(auto it = begin; it != end; ++it) {
				por::event::event const* e = *it;
				if(!e) {
					continue;
				}
				insert(*e);
			}
		}

		template<typename T>
		cone(util::iterator_range<T> range) : cone(range.begin(), range.end()) { }

		cone(por::event::event const& immediate_predecessor,
			por::event::event const* single_other_predecessor,
			util::iterator_range<por::event::event const* const*> other_predecessors);

		cone(por::configuration const& configuration);

		// IMPORTANT: assumes no conflict between this and rhs
		bool is_lte_for_all_of(cone const& rhs) const noexcept;

		// IMPORTANT: assumes no conflict between this and rhs
		bool is_gte_for_all_of(cone const& rhs) const noexcept;

		void extend_unchecked_single(por::event::event const& event) noexcept;

		por::comb setminus(cone const& rhs) const noexcept;
	};
}
