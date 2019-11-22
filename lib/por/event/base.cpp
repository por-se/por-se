#include "por/event/base.h"

#include "por/event/event.h"

#include "util/check.h"

#include <algorithm>
#include <cassert>
#include <set>
#include <stack>

namespace {
	bool lock_is_independent(por::event::event const* lock_event, por::event::event const* other) noexcept {
		assert(lock_event->kind() == por::event::event_kind::lock_create
			|| lock_event->kind() == por::event::event_kind::lock_acquire
			|| lock_event->kind() == por::event::event_kind::lock_release
			|| lock_event->kind() == por::event::event_kind::lock_destroy
			|| lock_event->kind() == por::event::event_kind::wait1
			|| lock_event->kind() == por::event::event_kind::wait2);

		// dependent iff events operate on same lock

		assert(lock_event->lid());

		if(other->lid() != 0) {
			return lock_event->lid() != other->lid();
		}

		// other is not a lock event
		return true;
	}

	bool lost_notification_is_independent(por::event::event const* lost, por::event::event const* other) noexcept {
		assert(lost->kind() == por::event::event_kind::signal
			|| lost->kind() == por::event::event_kind::broadcast);

		if(lost->kind() == por::event::event_kind::signal) {
			[[maybe_unused]] auto sig = static_cast<por::event::signal const*>(lost);
			assert(sig->is_lost());
		} else if(lost->kind() == por::event::event_kind::broadcast) {
			[[maybe_unused]] auto bro = static_cast<por::event::broadcast const*>(lost);
			assert(bro->is_lost());
		}

		if(other->kind() == por::event::event_kind::wait2) {
			// wait2 is always independent of lost notifications
			return true;
		}

		if(lost->cid() != other->cid()) {
			// cid must be equal for subsequent checks
			return true;
		}

		if(other->kind() == por::event::event_kind::signal) {
			auto sig = static_cast<por::event::signal const*>(other);
			if(sig->is_lost()) {
				return true;
			}
		} else if(other->kind() == por::event::event_kind::broadcast) {
			auto bro = static_cast<por::event::broadcast const*>(other);
			if(bro->is_lost()) {
				return true;
			}
		} else if(other->kind() == por::event::event_kind::wait1) {
			return false;
		}

		return lost->cid() != other->cid();
	}

	bool thread_is_independent(por::event::event const* a, por::event::event const* b) {
		// dependencies besides same-thread:
		// i:thread_create(j) is dependent on j:thread_init()
		// i:thread_exit() is dependent on all j:thread_join(i)
		// i:thread_join(j) is dependent on all k:thread_join(j) and j:thread_exit()
		// i:thread_init() is dependent on j:thread_create(i)

		por::event::event const* thread_event = nullptr;
		por::event::event const* other = nullptr;
		switch(a->kind()) {
			case por::event::event_kind::thread_create:
			case por::event::event_kind::thread_exit:
			case por::event::event_kind::thread_init:
			case por::event::event_kind::thread_join: {
				thread_event = a;
				other = b;
				break;
			}
			default: {
				break;
			}
		}
		if(!thread_event) {
			switch(b->kind()) {
				case por::event::event_kind::thread_create:
				case por::event::event_kind::thread_exit:
				case por::event::event_kind::thread_init:
				case por::event::event_kind::thread_join: {
					thread_event = b;
					other = a;
					break;
				}
				default: {
					// neither a nor b are thread event
					return true;
				}
			}
		}

		switch(thread_event->kind()) {
			case por::event::event_kind::thread_create: {
				auto create = static_cast<por::event::thread_create const*>(thread_event);
				if(other->kind() == por::event::event_kind::thread_init) {
					return create->created_tid() != other->tid();
				}
				return true;
			}
			case por::event::event_kind::thread_exit: {
				if(other->kind() == por::event::event_kind::thread_join) {
					auto join = static_cast<por::event::thread_join const*>(other);
					return thread_event->tid() != join->joined_thread();
				}
				return true;
			}
			case por::event::event_kind::thread_join: {
				auto join = static_cast<por::event::thread_join const*>(thread_event);
				if(other->kind() == por::event::event_kind::thread_join) {
					auto other_join = static_cast<por::event::thread_join const*>(other);
					return join->joined_thread() != other_join->joined_thread();
				} else if(other->kind() == por::event::event_kind::thread_exit) {
					return join->joined_thread() != other->tid();
				}
				return true;
			}
			case por::event::event_kind::thread_init: {
				if(other->kind() == por::event::event_kind::thread_create) {
					auto create = static_cast<por::event::thread_create const*>(other);
					return thread_event->tid() != create->created_tid();
				}
				return true;
			}
			default:
				return true;
		}
	}
}

namespace por::event {
	por::event::event::color_t event::_next_color = 1;
	por::event::event::color_t event::_imm_cfl_next_color = 1;

	event_iterator::event_iterator(event const& event, bool with_root, bool with_event, bool end)
	: _lc(&event), _with_root(with_root) {
		if(event.kind() == por::event::event_kind::program_init) {
			_with_root = false;
		}

		if(end) {
			return;
		}

		if(with_event) {
			_event = _lc;
		} else if(!_lc->cone().empty()) {
			_thread = _lc->cone().rbegin();
			_event = _thread->second;
		} else if(_with_root) {
			assert(_lc->kind() == por::event::event_kind::thread_init);
			_event = *_lc->predecessors().begin();
			assert(_event->kind() == por::event::event_kind::program_init);
			_thread = _lc->cone().rend();
		}
	}

	event_iterator& event_iterator::operator++() noexcept {
		if(!_event) {
			return *this;
		}

		if(_lc->cone().empty() || _thread == _lc->cone().rend()) {
			if(_event == _lc && _with_root) {
				assert(_lc->kind() == por::event::event_kind::thread_init);
				_event = *_lc->predecessors().begin();
				assert(_event->kind() == por::event::event_kind::program_init);
				_thread = _lc->cone().rend();
			} else {
				_thread = decltype(_thread)();
				_event = nullptr;
			}
		} else if(_event == _lc) {
			_thread = _lc->cone().rbegin();
			_event = _thread->second;
		} else if(por::event::event const* p = _event->thread_predecessor()) {
			_event = p;
			assert(_event);
		} else if(_thread != std::prev(_lc->cone().rend())) {
			++_thread;
			_event = _thread->second;
			assert(_event);
		} else if(_with_root) {
			libpor_check(_thread == std::prev(_lc->cone().rend()));
			libpor_check(_event->kind() == event_kind::thread_init);
			libpor_check((*_event->predecessors().begin())->kind() == event_kind::program_init);
			_thread = _lc->cone().rend();
			_event = *_event->predecessors().begin();
			assert(_event);
		} else {
			_thread = decltype(_thread)();
			_event = nullptr;
		}
		return *this;
	}

	bool event::is_independent_of(event const* other) const noexcept {
		if(tid() == other->tid()) {
			// events on the same thread are always dependent
			return false;
		}

		if(kind() == event_kind::local || other->kind() == event_kind::local) {
			// local events on different threads are always independent
			return true;
		}

		if(kind() == event_kind::program_init || other->kind() == event_kind::program_init) {
			// all events are dependent on program_init
			return false;
		}

		if(!thread_is_independent(this, other)) {
			// only return here if either this or other is a thread event
			return false;
		}

		switch(kind()) {
			case event_kind::lock_create:
			case event_kind::lock_acquire:
			case event_kind::lock_release:
			case event_kind::lock_destroy: {
				return lock_is_independent(this, other);
			}
			case event_kind::wait1: {
				if(!lock_is_independent(this, other)) {
					return false;
				}

				bool same_cid = (cid() == other->cid());
				switch(other->kind()) {
					case event_kind::signal: {
						auto sig = static_cast<signal const*>(other);
						if(!same_cid) {
							return true;
						}
						if(sig->is_lost()) {
							return false;
						}
						if(tid() == sig->notified_thread()) {
							return false;
						}
						return true;
					}
					case event_kind::broadcast:
					case event_kind::condition_variable_create:
					case event_kind::condition_variable_destroy: {
						return cid() != other->cid();
					}
					default: {
						return true;
					}
				}
			}
			case event_kind::wait2: {
				if(!lock_is_independent(this, other)) {
					return false;
				}

				bool same_cid = (cid() == other->cid());
				switch(other->kind()) {
					case event_kind::wait1:
					case event_kind::wait2: {
						// only dependent if operating on the same lock
						return true;
					}
					case event_kind::condition_variable_create:
					case event_kind::condition_variable_destroy: {
						return !same_cid;
					}
					case event_kind::signal: {
						auto sig = static_cast<signal const*>(other);
						return !same_cid || sig->notified_thread() != tid();
					}
					case event_kind::broadcast: {
						auto bro = static_cast<broadcast const*>(other);
						return !same_cid || !bro->is_notifying_thread(tid());
					}
					default: {
						return true;
					}
				}
			}
			case event_kind::signal: {
				auto sig = static_cast<signal const*>(this);

				if(sig->is_lost()) {
					return lost_notification_is_independent(this, other);
				}

				bool same_cid = (cid() == other->cid());
				switch(other->kind()) {
					case event_kind::wait1:
					case event_kind::wait2: {
						return !same_cid || (sig->notified_thread() != other->tid());
					}
					case event_kind::condition_variable_create:
					case event_kind::condition_variable_destroy:
					case event_kind::broadcast:
					case event_kind::signal: {
						return !same_cid;
					}
					default: {
						return true;
					}
				}
			}
			case event_kind::broadcast: {
				auto bro = static_cast<broadcast const*>(this);

				if(bro->is_lost()) {
					return lost_notification_is_independent(this, other);
				}

				bool same_cid = (cid() == other->cid());
				switch(other->kind()) {
					case event_kind::wait1: {
						return !same_cid;
					}
					case event_kind::wait2: {
						return !same_cid || !bro->is_notifying_thread(other->tid());
					}
					case event_kind::condition_variable_create:
					case event_kind::condition_variable_destroy:
					case event_kind::broadcast:
					case event_kind::signal: {
						return !same_cid;
					}
					default: {
						return true;
					}
				}
			}
			case event_kind::condition_variable_create:
			case event_kind::condition_variable_destroy: {
				return (cid() == 0) || (cid() != other->cid());
			}

			case event_kind::thread_create:
			case event_kind::thread_init:
			case event_kind::thread_exit:
			case event_kind::thread_join: {
				return thread_is_independent(this, other);
			}

			default: {
				assert(0 && "not implemented");
				return false;
			}
		}
	}

	bool event::is_enabled(por::configuration const& configuration) const noexcept {
		por::cone C(configuration);
		if(std::any_of(cone().begin(), cone().end(), [&C](auto &pair) {
			por::event::event const* p = pair.second;
			if(!C.has(p->tid())) {
				return true;
			}
			return p->depth() > C.at(p->tid())->depth();
		})) {
			// gap in depth between configuration and cone
			return false;
		}

		for(auto &p : immediate_predecessors()) {
			assert(C.has(p->tid()) && p->depth() <= C.at(p->tid())->depth()); // imm pred are subset of cone
			por::event::event const* e = C.at(p->tid());
			while(e != p) {
				if(e == nullptr) {
					return false;
				}
				e = e->thread_predecessor();
			}
		}
		return true;
	}

	std::size_t event::mark_as_cutoff() const noexcept {
		if(_is_cutoff) {
			return 0;
		}

		std::size_t new_cutoffs = 0;

		std::vector<event const*> W{this};
		while(!W.empty()) {
			auto w = W.back();
			W.pop_back();

			w->_is_cutoff = true;
			++new_cutoffs;

			for(auto s : w->successors()) {
				if(!s->is_cutoff()) {
					W.push_back(s);
				}
			}
		}
		return new_cutoffs;
	}

	std::vector<event const*> event::immediate_predecessors() const noexcept {
		std::vector<event const*> result;
		[[maybe_unused]] bool program_init = false;
		for(auto& p : predecessors()) {
			if(p->kind() == event_kind::program_init) {
				assert(!program_init);
				program_init = true;
				result.push_back(p);
			} else if(p == _cone.at(p->tid())) {
				if(std::find(result.begin(), result.end(), p) != result.end()) {
					continue;
				}
				result.push_back(p);
			}
		}
		libpor_check(result.size() <= _cone.size() || (program_init && result.size() <= _cone.size() + 1));
		libpor_check(result.size() <= predecessors().size());
		return result;
	}

	std::vector<event const*> event::compute_immediate_conflicts_sup(event const* find) const noexcept {
		color_t blue = _imm_cfl_next_color++;
		color_t red = _imm_cfl_next_color++;

		std::vector<event const*> W(causes_begin(), causes_end());
		for(auto& w : W) {
			w->_imm_cfl_color = red;
		}

		std::vector<event const*> result;

		while(!W.empty()) {
			auto event = W.back();
			W.pop_back();

			assert(event != nullptr);

			for(auto& succ : event->successors()) {
				if(succ == this || succ->_imm_cfl_color == red || succ->_imm_cfl_color == blue) {
					continue;
				}

				if(auto preds = succ->predecessors(); std::any_of(preds.begin(), preds.end(), [this, &red](auto& e) {
					// non-red predecessor => cannot determine yet whether succ is in causes(e) or concurrent to e
					return e->_imm_cfl_color != red;
				})) {
					continue;
				}

				if(is_independent_of(succ)) {
					libpor_check(succ->is_independent_of(this));
					succ->_imm_cfl_color = red;
					W.push_back(succ);
				} else {
					if(succ == find) {
						return {find};
					}
					succ->_imm_cfl_color = blue;
					result.push_back(succ);
				}
			}
		}

#ifdef LIBPOR_CHECKED
		std::sort(result.begin(), result.end());
		libpor_check(std::adjacent_find(result.begin(), result.end()) == result.end() && "absence of duplicates");
		libpor_check(std::find(result.begin(), result.end(), this) == result.end() && "event is in immediate conflict with itself");
		libpor_check(std::none_of(result.begin(), result.end(), [this](auto& e) {
			auto lc = e->local_configuration();
			return std::find(lc.begin(), lc.end(), this) != lc.end();
		}) && "conflicting event is causally dependent on this");
#endif

		if(find != nullptr) {
			return {};
		}
		return result;
	}
}
