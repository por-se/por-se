#include "include/por/cone.h"
#include "include/por/event/event.h"

using namespace por;

void cone::insert(por::event::event const& p) {
	for(auto& [tid, event] : p.cone()) {
		if(_map.count(tid) == 0 || _map[tid]->depth() < event->depth()) {
			_map[tid] = event;
		}
	}

	// p is not yet part of cone
	if(_map.count(p.tid()) == 0 || _map[p.tid()]->depth() < p.depth()) {
		_map[p.tid()] = &p;
	}
}

cone::cone(por::event::event const& immediate_predecessor)
: _map(immediate_predecessor.cone()._map)
{
	// immediate_predecessor may be on different thread than this new event, e.g. in thread_init
	_map[immediate_predecessor.tid()] = &immediate_predecessor;
}

cone::cone(util::iterator_range<por::event::event const* const*> events)
{
	for(auto& e : events) {
		if(e != nullptr) {
			insert(*e);
		}
	}
}

cone::cone(por::event::event const& immediate_predecessor,
           por::event::event const* single_other_predecessor,
           util::iterator_range<por::event::event const* const*> other_predecessors)
: _map(immediate_predecessor.cone()._map)
{
	_map[immediate_predecessor.tid()] = &immediate_predecessor;

	if(single_other_predecessor) {
		insert(*single_other_predecessor);
	}

	for(auto& op : other_predecessors) {
		if(op != nullptr) {
			insert(*op);
		}
	}
}

bool cone::is_lte_for_all_of(cone const& rhs) const noexcept {
	for(auto& [tid, event] : rhs) {
		if(at(tid)->depth() > event->depth()) {
			// By construction, rhs also includes all elements of event's cone.
			// Thus, we only need to check against the depth on the same tid.
			return false;
		}
		assert(!count(tid) || at(tid) == event || at(tid)->is_less_than_eq(*event));
	}
	return true;
}

bool cone::is_gte_for_all_of(cone const& rhs) const noexcept {
	for(auto& [tid, event] : rhs) {
		if(!count(tid) || at(tid)->depth() < event->depth()) {
			// By construction, rhs also includes all elements of event's cone.
			// Thus, we only need to check against the depth on the same tid.
			return false;
		}
		assert(at(tid) == event || event->is_less_than_eq(*at(tid)));
	}
	return true;
}

void cone::extend_unchecked_single(por::event::event const& event) noexcept {
#ifndef NDEBUG // FIXME: expensive
	assert(is_lte_for_all_of(event.cone()));
#endif
	assert(!_map.count(event.tid()) || _map[event.tid()]->depth() <= event.depth());
	_map[event.tid()] = &event;
}
