
#include "include/por/unfolding.h"
#include "include/por/event/event.h"

#include <algorithm>

using namespace por;

unfolding::unfolding() {
	_root = store_event(por::event::program_init{});
	mark_as_explored(*_root);
}

// NOTE: do not use for other purposes, only compares pointers of predecessors
bool unfolding::compare_events(por::event::event const& a, por::event::event const& b) {
	if(&a == &b)
		return true;

	if(a.tid() != b.tid())
		return false;

	if(a.depth() != b.depth())
		return false;

	if(a.kind() != b.kind())
		return false;

	if(a.kind() == por::event::event_kind::local) {
		auto& alocal = static_cast<por::event::local const&>(a);
		auto& blocal = static_cast<por::event::local const&>(b);

		if(alocal.path() != blocal.path())
			return false;
	}

	auto a_preds = a.predecessors();
	auto b_preds = b.predecessors();
	std::size_t a_num_preds = std::distance(a_preds.begin(), a_preds.end());
	std::size_t b_num_preds = std::distance(b_preds.begin(), b_preds.end());

	if(a_num_preds != b_num_preds)
		return false;

	auto a_it = a_preds.begin();
	auto b_it = b_preds.begin();
	[[maybe_unused]] auto a_ie = a_preds.end();
	[[maybe_unused]] auto b_ie = b_preds.end();
	for(std::size_t i = 0; i < a_num_preds; ++i) {
		assert(a_it != a_ie);
		assert(b_it != b_ie);
		if(*a_it != *b_it)
			return false;
		++a_it;
		++b_it;
	}

	return true;
}
