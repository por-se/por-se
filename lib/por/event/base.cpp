#include "por/event/base.h"

#include "por/event/event.h"

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

		switch(other->kind()) {
			case por::event::event_kind::lock_create:
			case por::event::event_kind::lock_acquire:
			case por::event::event_kind::lock_release:
			case por::event::event_kind::lock_destroy:
			case por::event::event_kind::wait1:
			case por::event::event_kind::wait2: {
				break;
			}
			default: {
				// other is not a lock event
				return true;
			}
		}

		por::event::event const* max = lock_event;
		por::event::event const* min = other;
		if(max->is_less_than(*min)) {
			std::swap(max, min);
		} else if(!min->is_less_than(*max)) {
			// no causal dependency
			return true;
		}

		// descent lock chain from <-max event
		por::event::event const* pred = max;
		do {
			if(pred == min) {
				// one event part of other lock chain
				return false;
			}
			pred = pred->lock_predecessor();
		} while(pred && min->is_less_than_eq(*pred));

		// distinct lock chains
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
	std::size_t event::_next_color = 1;

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

	std::set<event const*> event::local_configuration() const noexcept {
		std::stack<event const*> W;
		std::set<event const*> result;
		std::size_t red = next_color++;

		W.push(this);
		while(!W.empty()) {
			auto event = W.top();
			W.pop();

			assert(event != nullptr);

			if(event->_color == red) {
				continue;
			}

			for(auto& pred : event->predecessors()) {
				if(pred == nullptr) {
					continue;
				}
				if(pred->_color != red) {
					W.push(pred);
				}
			}
			result.insert(event);
			event->_color = red;
		}

		return result;
	}

	std::vector<event const*> event::immediate_predecessors() const noexcept {
		std::vector<event const*> result;
		for(auto& p : predecessors()) {
			if(p == _cone.at(p->tid())) {
				if(std::find(result.begin(), result.end(), p) != result.end()) {
					continue;
				}
				result.push_back(p);
			}
		}
		assert(result.size() <= _cone.size());
		assert(result.size() <= predecessors().size());
		return result;
	}
}
