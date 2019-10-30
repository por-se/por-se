#include "include/por/unfolding.h"

#include "include/por/configuration.h"
#include "include/por/event/event.h"

#include <algorithm>

using namespace por;

unfolding::unfolding() {
	_root = store_event(por::event::program_init{});
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

	if(a_preds.size() != b_preds.size())
		return false;

	auto a_it = a_preds.begin();
	auto b_it = b_preds.begin();
	[[maybe_unused]] auto a_ie = a_preds.end();
	[[maybe_unused]] auto b_ie = b_preds.end();
	for(std::size_t i = 0; i < a_preds.size(); ++i) {
		assert(a_it != a_ie);
		assert(b_it != b_ie);
		if(*a_it != *b_it)
			return false;
		++a_it;
		++b_it;
	}

	return true;
}

namespace {
	// checks whether [local_configuration] \cup set contains all causal predecessors of set
	bool union_contains_predecessors_of_set(por::event::event const& local_configuration, std::vector<por::event::event const*> set) {
		por::cone cone = local_configuration.cone();
		cone.extend_unchecked_single(local_configuration);

		while(!set.empty()) {
			bool event_added = false;
			// FIXME: improve with erase(std::remove_if())
			for(auto it = set.begin(); it != set.end(); ) {
				assert(*it != nullptr);
				auto const& e = **it;
				if(cone.is_gte_for_all_of(e.cone())) {
					// all predecessors of e are already in the cone, only e is new
					cone.insert(e);
					it = set.erase(it);
					event_added = true;
				} else {
					++it;
				}
			}
			if(!event_added) {
				// not a single event could be added in iteration
				return false;
			}
		}

		return true;
	}
}

por::event::event const*
unfolding::compute_alternative(por::configuration const& c, std::vector<por::event::event const*> D) const noexcept {
	assert(!D.empty());
	std::vector<por::event::event const*> C(c.begin(), c.end());
	std::size_t red = por::event::event::colorize(C.cbegin(), C.cend());
	for(auto d : D) {
		// Choose some event d \in D such that d does not conflict with any event in C
		assert(d != nullptr);
		assert(d->color() != red); // D \cap C = \emptyset
		auto imm = d->immediate_conflicts();
		if(std::any_of(imm.begin(), imm.end(), [&red](auto cfl) {
			return cfl->color() == red;
		})) {
			// d is in immediate conflict with some event in C
			continue;
		}

		// find an event e (\in U \setminus D) in immediate conflict with d
		for(auto& e : d->immediate_conflicts()) {
			assert(e != nullptr);
			if(std::find(D.begin(), D.end(), e) != D.end()) {
				// e is not in U \setminus D
				continue;
			}

			// check if [e] \cup C is a valid configuration
			// 1. check if [e] \cup C contains all predecessors of C
			if(!union_contains_predecessors_of_set(*e, C)) {
				continue;
			}

			// 2. check if [e] \cup C is conflict free
			std::vector<por::event::event const*> cex;
			for(auto& x : c.conflicting_extensions()) {
				cex.push_back(&x.new_event());
			}
			std::size_t blue = por::event::event::colorize(cex.begin(), cex.end());
			bool is_conflict_free = true;
			std::vector<por::event::event const*> W{e};
			while(!W.empty()) {
				auto w = W.back();
				W.pop_back();

				if(w->color() == red) {
					// predecessors of w cannot be in conflict with events in C
					continue;
				}

				if(w->color() == blue) {
					// e conflicts with some event in C
					is_conflict_free = false;
					break;
				}

				for(auto& p : w->immediate_predecessors()) {
					W.push_back(p);
				}
			}
			if(is_conflict_free) {
				return e;
			}
		}
	}
	return nullptr;
}
