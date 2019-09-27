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
