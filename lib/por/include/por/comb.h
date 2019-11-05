#pragma once

#include "por/thread_id.h"

#include <util/iterator_range.h>

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
		using iterator = decltype(_events)::const_iterator;
		using const_iterator = iterator;

		iterator begin() const { return _events.cbegin(); }
		iterator end() const { return _events.cend(); }
		auto size() const { return _events.size(); }
		auto empty() const { return _events.empty(); }

		bool insert(por::event::event const& event) noexcept;

		void remove(por::event::event const& event) noexcept;

		bool is_sorted() const noexcept;
		por::event::event const* min() const noexcept { return empty() ? nullptr : _events.front(); }
		por::event::event const* max() const noexcept { return empty() ? nullptr : _events.back(); }
		void sort() noexcept;
	};

	class comb;

	class comb_iterator {
		por::comb const* _comb = nullptr;
		std::map<thread_id, tooth>::const_iterator _tooth;
		por::tooth::const_iterator _event;

	public:
		using value_type = por::event::event const*;
		using difference_type = std::ptrdiff_t;
		using pointer = por::event::event const* const*;
		using reference = por::event::event const* const&;

		using iterator_category = std::forward_iterator_tag;

		comb_iterator() = default;
		explicit comb_iterator(por::comb const& comb, bool end=false);

		reference operator*() const noexcept { return *_event; }
		pointer operator->() const noexcept { return &*_event; }

		comb_iterator& operator++() noexcept;
		comb_iterator operator++(int) noexcept {
			comb_iterator tmp = *this;
			++(*this);
			return tmp;
		}

		bool operator==(const comb_iterator& rhs) const noexcept {
			return _comb == rhs._comb && _tooth == rhs._tooth && _event == rhs._event;
		}
		bool operator!=(const comb_iterator& rhs) const noexcept {
			return !(*this == rhs);
		}
	};

	class comb {
		std::map<thread_id, tooth> _teeth;

	public:
		using iterator = comb_iterator;
		using const_iterator = iterator;

		comb() = default;
		comb(const comb&) = default;
		comb(comb const& comb, std::function<bool(por::event::event const&)> filter);
		comb(comb&&) = default;
		comb& operator=(const comb&) = default;
		comb& operator=(comb&&) = default;
		~comb() = default;

		iterator begin() const noexcept { return comb_iterator(*this); }
		iterator end() const noexcept { return comb_iterator(*this, true); }

		auto threads_begin() const noexcept { return _teeth.cbegin(); }
		auto threads_end() const noexcept { return _teeth.cend(); }

		auto threads() const noexcept { return util::make_iterator_range(threads_begin(), threads_end()); }

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
			return std::all_of(threads_begin(), threads_end(), [](auto& t) { return t.second.is_sorted(); });
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
