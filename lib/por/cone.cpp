#include "include/por/cone.h"

#include "include/por/configuration.h"
#include "include/por/comb.h"
#include "include/por/event/event.h"

#include "util/check.h"

using namespace por;

void cone::insert(por::event::event const& p) {
	if(p.kind() == por::event::event_kind::program_init) {
		return;
	}

	for(auto& [tid, event] : p.cone()) {
		if(!has(tid) || at(tid)->depth() < event->depth()) {
			_map[tid] = event;
		}
	}

	// p is not yet part of cone
	if(!has(p.tid()) || at(p.tid())->depth() < p.depth()) {
		_map[p.tid()] = &p;
	}
}

cone::cone(por::event::event const& immediate_predecessor)
: _map(immediate_predecessor.cone()._map)
{
	if(immediate_predecessor.kind() == por::event::event_kind::program_init) {
		return;
	}

	// immediate_predecessor may be on different thread than this new event, e.g. in thread_init
	_map[immediate_predecessor.tid()] = &immediate_predecessor;
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

cone::cone(por::configuration const& configuration) : _map(configuration.thread_heads()) { }

bool cone::is_lte_for_all_of(cone const& rhs) const noexcept {
	for(auto& [tid, event] : rhs) {
		if(has(tid) && at(tid)->depth() > event->depth()) {
			// By construction, rhs also includes all elements of event's cone.
			// Thus, we only need to check against the depth on the same tid.
			return false;
		}
		libpor_check(!has(tid) || at(tid) == event || at(tid)->is_less_than_eq(*event));
	}
	return true;
}

bool cone::is_gte_for_all_of(cone const& rhs) const noexcept {
	for(auto& [tid, event] : rhs) {
		if(!has(tid) || at(tid)->depth() < event->depth()) {
			// By construction, rhs also includes all elements of event's cone.
			// Thus, we only need to check against the depth on the same tid.
			return false;
		}
		libpor_check(at(tid) == event || event->is_less_than_eq(*at(tid)));
	}
	return true;
}

void cone::extend_unchecked_single(por::event::event const& event) noexcept {
	libpor_check(is_lte_for_all_of(event.cone()));
	assert(event.kind() != por::event::event_kind::program_init);
	assert(!has(event.tid()) || at(event.tid())->depth() <= event.depth());
	_map[event.tid()] = &event;
}

// computes a comb of [*this] \setminus [rhs]
por::comb cone::setminus(por::cone const& rhs) const noexcept {
	por::comb result;
	for(auto& [tid, event] : *this) {
		if(!rhs.has(tid)) {
			// no event on tid removed by rhs
			por::event::event const* e = event;
			do {
				result.insert(*e);
				e = e->thread_predecessor();
			} while(e);
			continue;
		}

		por::event::event const* r = rhs.at(tid);

		if(r->depth() > event->depth()) {
			// all events on tid removed by rhs
			continue;
		}

		por::event::event const* e = event;
		while(e && r->depth() < e->depth()) {
			result.insert(*e);
			e = e->thread_predecessor();
		}
	}
	return result;
}
