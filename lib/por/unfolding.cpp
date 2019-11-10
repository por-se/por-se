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

por::event::event const*
unfolding::compute_alternative(por::configuration const& c, std::vector<por::event::event const*> D) const noexcept {
	assert(!D.empty());
	std::vector<por::event::event const*> C(c.begin(), c.end());
	auto red = por::event::event::colorize(C.cbegin(), C.cend());

	auto blue = por::event::event::new_color();
	for(auto c : C) {
		for(auto x : c->immediate_conflicts()) {
			x->colorize(blue);
		}
	}

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
		for(auto& e : imm) {
			if(e->color() == blue) {
				// e is in conflict with C
				continue;
			}

			if(std::find(D.begin(), D.end(), e) != D.end()) {
				// e is not in U \setminus D
				continue;
			}

#ifndef NDEBUG // FIXME: expensive
			// check if e is an extension of C
			assert(std::find(C.begin(), C.end(), e) == C.end());
#endif

			// check if [e] \cup C is a valid configuration
			// we already know that C is a valid configuration
			// thus: [e] is not in conflict with C => [e] \cup C is a valid configuration
			// also: there ex. no event x in ([e] \setminus C) with x #_i c \in C => [e] \cup C is a valid configuration
			bool is_conflict_free = true;
			std::set<por::event::event const*> imm_conflicts;
			std::vector<por::event::event const*> W{e};
			while(is_conflict_free && !W.empty()) {
				auto w = W.back();
				W.pop_back();

				if(w->color() == red) {
					// predecessors of w cannot be in conflict with events in C
					continue;
				}

				if(w->color() == blue) {
					// w is in conflict with C
					is_conflict_free = false;
					break;
				}

				for(auto& p : w->immediate_predecessors()) {
					for(auto& x : p->immediate_conflicts()) {
						if(x->color() == red) {
							// conflict of candidate e is in C
							is_conflict_free = false;
							break;
						}
						imm_conflicts.insert(x);
					}
					if(!is_conflict_free) {
						break;
					}
					W.push_back(p);
				}
			}

			if(is_conflict_free) {
				for(auto x : imm_conflicts) {
					if(x->color() == red) {
						is_conflict_free = false;
						break;
					}
				}
			}

			if(is_conflict_free) {
				return e;
			}
		}
	}
	return nullptr;
}
