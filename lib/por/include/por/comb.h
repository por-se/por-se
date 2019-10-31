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
		// thus: _events[0] < {_events[1], ..., _events[n-1]} < _events[n]
		std::deque<por::event::event const*> _events;

		// true iff all events are sorted (by causality)
		bool _sorted = true;

	public:
		auto begin() const { return _events.begin(); }
		auto end() const { return _events.end(); }
		auto size() const { return _events.size(); }
		auto empty() const { return _events.empty(); }

		bool insert(por::event::event const& event) noexcept;

		void remove(por::event::event const& event) noexcept;

		bool is_sorted() const noexcept;
		por::event::event const* min() const noexcept { return empty() ? nullptr : _events.front(); }
		por::event::event const* max() const noexcept { return empty() ? nullptr : _events.back(); }
		void sort() noexcept;
	};

	class comb {
		std::map<thread_id, tooth> _teeth;

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

		std::size_t num_threads() const noexcept { return _teeth.size(); }
		std::size_t size() const noexcept {
			std::size_t n = 0;
			for(auto& [_, tooth] : _teeth) {
				assert(!tooth.empty());
				n += tooth.size();
			}
			return n;
		}
		bool empty() const noexcept {
			assert(_teeth.empty() == (size() == 0));
			return _teeth.empty();
		}

		auto find(thread_id const& tid) const { return _teeth.find(tid); }
		auto& at(thread_id const& tid) const { return _teeth.at(tid); }
		auto count(thread_id const& tid) const { return _teeth.count(tid); }

		void insert(por::event::event const& event) noexcept;

		bool is_sorted() const noexcept {
			// FIXME: value can be cached if we prevent teeth from being modified (or monitor changes)
			return std::all_of(begin(), end(), [](auto& t) { return t.second.is_sorted(); });
		}
		std::vector<por::event::event const*> min() const noexcept;
		std::vector<por::event::event const*> max() const noexcept;
		void sort() noexcept;

		void remove(por::event::event const& event) noexcept;

		template<typename T>
		void remove(T begin, T end) {
			for (; begin != end; ++begin) {
				if(_teeth.count((*begin)->tid())) {
					_teeth[(*begin)->tid()].remove(**begin);
					if(_teeth[(*begin)->tid()].empty()) {
						_teeth.erase((*begin)->tid());
					}
				}
			}
		}

		// compute all combinations: S \subseteq comb (where S is concurrent,
		// i.e. there are no causal dependencies between any of its elements)
		// IMPORTANT: comb must be conflict-free
		std::vector<std::vector<por::event::event const*>>
		concurrent_combinations(std::function<bool(std::vector<por::event::event const*>&)> filter);
	};
}
