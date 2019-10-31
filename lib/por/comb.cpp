#include "include/por/comb.h"
#include "include/por/event/event.h"

#include <algorithm>
#include <cassert>

using namespace por;

bool tooth::insert(por::event::event const& e) noexcept {
	if(_events.empty()) {
		// order is preserved
		assert(_sorted);
		_events.push_back(&e);
	} else if(e.is_less_than(*_events.front())) {
		// order is preserved
		_events.push_front(&e);
	} else if((_events.size() == 1 && _events.front() != &e) || _events.back()->is_less_than(e)) {
		// order is preserved
		_events.push_back(&e);
	} else if(std::find(_events.begin(), _events.end(), &e) != _events.end()) {
		// event is already present
		return _sorted;
	} else {
		_events.insert(std::prev(_events.end()), &e);
		if(_events.size() == 3) {
			// order is preserved
			assert(_sorted);
		} else {
			assert(_events.size() > 3);
			_sorted = false;
		}
	}
	assert(_sorted == is_sorted()); // for invariant checks
	return _sorted;
}

void tooth::remove(por::event::event const& e) noexcept {
	using it_t = decltype(_events)::const_iterator;
	auto cmp = [](it_t a, it_t b) { return (*a)->is_less_than(**b); };

	auto it = std::find(_events.begin(), _events.end(), &e);
	if(it == _events.end()) {
		return;
	} else if(_sorted || _events.size() <= 3) {
		_events.erase(it);
		_sorted = true;
	} else if(it != _events.begin() && it != std::prev(_events.end())) {
		if(_events.size() == 4) {
			_sorted = true;
		}
		_events.erase(it);
	} else if(it == _events.begin()) {
		_events.pop_front();
		auto min = std::min(_events.begin(), _events.end(), cmp);
		std::iter_swap(min, _events.begin());
	} else if(it == std::prev(_events.end())) {
		_events.pop_back();
		auto max = std::max(_events.begin(), _events.end(), cmp);
		std::iter_swap(max, std::prev(_events.end()));
	}
	assert(_sorted == is_sorted()); // for invariant checks
}

void tooth::sort() noexcept {
	if(_sorted) {
		assert(_sorted == is_sorted()); // for invariant checks
		return;
	}
	std::sort(_events.begin(), _events.end(), [](auto& a, auto& b) {
		return a->is_less_than(*b);
	});
	_sorted = true;
}

bool tooth::is_sorted() const noexcept {
#ifndef NDEBUG // FIXME: EXPENSIVE
	assert(std::all_of(std::next(begin()), end(), [this](auto* e) { return _events.front()->is_less_than(*e); }));
	assert(std::all_of(begin(), std::prev(end()), [this](auto* e) { return e->is_less_than(*_events.back()); }));
	if(_sorted) {
		std::vector<por::event::event const*> tmp(begin(), end());
		std::sort(tmp.begin(), tmp.end(), [](auto a, auto b) { return a->is_less_than(*b); });
		assert(std::equal(tmp.begin(), tmp.end(), begin()));
	}
#endif
	return _sorted;
}

comb::comb(comb const& comb, std::function<bool(por::event::event const&)> filter) {
	for(auto& [tid, tooth] : comb) {
		for(auto const& event : tooth) {
			if(filter(*event)) {
				insert(*event);
			}
		}
	}
}

void comb::insert(por::event::event const& e) noexcept {
	_teeth[e.tid()].insert(e);
}

void comb::sort() noexcept {
	for(auto& [tid, tooth] : _teeth) {
		tooth.sort();
	}
}

std::vector<por::event::event const*> comb::max() const noexcept {
	std::vector<por::event::event const*> result;
	for(auto& [tid, tooth] : _teeth) {
		por::event::event const* tmax = tooth.max();
		bool is_maximal_element = true;
		for(auto it = result.begin(); it != result.end();) {
			if((*it)->is_less_than(*tmax)) {
				it = result.erase(it);
			} else {
				if(tmax->is_less_than(**it)) {
					is_maximal_element = false;
					break;
				}
				++it;
			}
		}
		if(is_maximal_element) {
			result.push_back(tmax);
		}
	}

#ifndef NDEBUG // FIXME: expensive?
	for(auto& a : result) {
		for(auto& b : result) {
			if(a == b) {
				continue;
			}
			assert(!a->is_less_than_eq(*b) && !b->is_less_than_eq(*a));
		}
	}
#endif

	return result;
}

std::vector<por::event::event const*> comb::min() const noexcept {
	std::vector<por::event::event const*> result;
	for(auto& [tid, tooth] : _teeth) {
		assert(!tooth.empty());
		por::event::event const* tmin = tooth.min();
		bool is_minimal_element = true;
		for(auto it = result.begin(); it != result.end();) {
			if(tmin->is_less_than(**it)) {
				it = result.erase(it);
			} else {
				if((*it)->is_less_than(*tmin)) {
					is_minimal_element = false;
					break;
				}
				++it;
			}
		}
		if(is_minimal_element) {
			result.push_back(tmin);
		}
	}

#ifndef NDEBUG // FIXME: expensive?
	for(auto& a : result) {
		for(auto& b : result) {
			if(a == b) {
				continue;
			}
			assert(!a->is_less_than_eq(*b) && !b->is_less_than_eq(*a));
		}
	}
#endif

	return result;
}

void comb::remove(por::event::event const& event) noexcept {
	_teeth[event.tid()].remove(event);
	if(_teeth[event.tid()].empty()) {
		_teeth.erase(event.tid());
	}
}

std::vector<std::vector<por::event::event const*>>
comb::concurrent_combinations(std::function<bool(std::vector<por::event::event const*>&)> filter) {
	std::vector<std::vector<por::event::event const*>> result;

	// the following operations assume that the comb is sorted
	// FIXME: WHY?!
	sort();

	assert(num_threads() < 64); // FIXME: can "only" be used with 64 threads
	for(std::uint64_t mask = 0; mask < (static_cast<std::uint64_t>(1) << num_threads()); ++mask) {
		std::size_t popcount = 0;
		for(std::size_t i = 0; i < num_threads(); ++i) {
			if((mask >> i) & 1)
				++popcount;
		}
		if (popcount > 0) {
			// indexes of the threads enabled in current mask
			// (of which there are popcount-many)
			std::vector<por::event::thread_id_t> selected_threads;
			selected_threads.reserve(popcount);

			// maps a selected thread to the highest index present in its event vector
			// i.e. highest_index[i] == comb[selected_threads[i]].size() - 1
			std::vector<std::size_t> highest_index;
			highest_index.reserve(popcount);

			auto it = begin();
			for(std::size_t i = 0; i < num_threads(); ++i, ++it) {
				assert(std::next(begin(), i) == it);
				assert(std::next(begin(), i) != end());
				if((mask >> i) & 1) {
					selected_threads.push_back(it->first);
					highest_index.push_back(it->second.size() - 1);
				}
			}

			// index in the event vector of corresponding thread for
			// each selected thread, starting with all zeros
			std::vector<std::size_t> event_indices(popcount, 0);

			std::size_t pos = 0;
			while(pos < popcount) {
				// complete subset
				std::vector<por::event::event const*> subset;
				subset.reserve(popcount);
				bool is_concurrent = true;
				for(std::size_t k = 0; k < popcount; ++k) {
					auto& new_event = at(selected_threads[k])._events[event_indices[k]];
					if(k > 0) {
						// check if new event is concurrent to previous ones
						for(auto& e : subset) {
							if(e->is_less_than(*new_event) || new_event->is_less_than(*e)) {
								is_concurrent = false;
								break;
							}
						}
					}
					if(!is_concurrent)
						break;
					subset.push_back(new_event);
				}
				if(is_concurrent && filter(subset)) {
					result.push_back(std::move(subset));
				}

				// search for lowest position that can be incremented
				while(pos < popcount && event_indices[pos] == highest_index[pos]) {
					++pos;
				}

				if(pos == popcount && event_indices[pos - 1] == highest_index[pos - 1])
					break;

				++event_indices[pos];

				// reset lower positions and go back to pos = 0
				while(pos > 0) {
					--pos;
					event_indices[pos] = 0;
				}
			}
		} else {
			// empty set
			std::vector<por::event::event const*> empty;
			if(filter(empty)) {
				result.push_back(std::move(empty));
			}
		}
	}
	return result;
}
