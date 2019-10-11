#pragma once

#include "por/thread_id.h"

#include <deque>
#include <functional>
#include <map>
#include <vector>

namespace por::event {
	class event;
}

namespace por {
	// IMPORTANT: assumes that set of stored events is conflict free
	class tooth {
		friend class comb;

		// invariant: first element is minimum, last element is maximum
		// thus: _events[0] < _events[1] <= ... <= _events[n-1] < _events[n]
		std::deque<por::event::event const*> _events;

		// true iff all events are sorted (by causality)
		bool _sorted = true;

	public:
		auto begin() const { return _events.begin(); }
		auto end() const { return _events.end(); }
		auto size() const { return _events.size(); }
		auto empty() const { return _events.empty(); }

		bool insert(por::event::event const& event) noexcept;

		bool is_sorted() const noexcept { return _sorted; }
		por::event::event const* min() const noexcept { return empty() ? nullptr : _events.front(); }
		por::event::event const* max() const noexcept { return empty() ? nullptr : _events.back(); }
		void sort() noexcept;
	};

	class comb {
		std::map<thread_id, tooth> _teeth;

		// true iff all teeth are sorted
		bool _sorted = true;

	public:
		comb() = default;
		comb(const comb&) = default;
		comb(comb const& comb, std::function<bool(por::event::event const&)> filter);
		comb(comb&&) = default;
		comb& operator=(const comb&) = default;
		comb& operator=(comb&&) = default;
		~comb() = default;

		auto begin() const { return _teeth.begin(); }
		auto end() const { return _teeth.end(); }
		auto size() const { return _teeth.size(); }
		auto empty() const { return _teeth.empty(); }

		auto find(thread_id const& tid) const { return _teeth.find(tid); }
		auto& at(thread_id const& tid) const { return _teeth.at(tid); }
		auto count(thread_id const& tid) const { return _teeth.count(tid); }

		void insert(por::event::event const& event) noexcept;

		bool is_sorted() const noexcept { return _sorted; }
		std::vector<por::event::event const*> min() const noexcept;
		std::vector<por::event::event const*> max() const noexcept;
		void sort() noexcept;

		// compute all combinations: S \subseteq comb (where S is concurrent,
		// i.e. there are no causal dependencies between any of its elements)
		// IMPORTANT: comb must be conflict-free
		std::vector<std::vector<por::event::event const*>>
		concurrent_combinations(std::function<bool(std::vector<por::event::event const*>&)> filter);
	};
}
