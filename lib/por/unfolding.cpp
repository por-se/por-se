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
		if(!a.has_same_local_path(b)) {
			return false;
		}
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
	bool in_immediate_conflict_with_color(por::event::event const& e, por::event::event::color_t color) {
		auto imm = e.immediate_conflicts();
		return std::any_of(imm.begin(), imm.end(), [&color](auto cfl) {
			return cfl->color() == color;
		});
	}
}

por::event::event const*
unfolding::compute_alternative(por::configuration const& c, std::vector<por::event::event const*> D) const noexcept {
	assert(!D.empty());
	std::vector<por::event::event const*> C(c.begin(), c.end());
	auto red = por::event::event::colorize(C.cbegin(), C.cend());
	auto blue = por::event::event::colorize(D.begin(), D.end());

	por::event::event const* e = nullptr;
	for(auto d : D) {
		assert(!d->ends_atomic_operation());
		if(!in_immediate_conflict_with_color(*d, red)) {
			e = d;
			break;
		}
	}
	assert(e != nullptr);

	por::event::event const* ep = nullptr;
	for(auto f : e->immediate_conflicts()) {
		assert(f->color() != red); // f should not be in C

		// determine if f is in conflict with some event in C or intersects with D
		bool in_conflict = false;
		std::vector<por::event::event const*> W{f};
		while(!W.empty()) {
			auto w = W.back();
			W.pop_back();

			if(w->color() == red) {
				// predecessors of w cannot be in D or in conflict with C
				continue;
			}

			if(w->color() == blue || in_immediate_conflict_with_color(*w, red)) {
				in_conflict = true;
				break;
			}

			for(auto p : w->immediate_predecessors()) {
				W.push_back(p);
			}
		}

		if(!in_conflict) {
			ep = f;
			break;
		}
	}

	if(ep != nullptr) {
		return ep;
	}

	return nullptr;
}
