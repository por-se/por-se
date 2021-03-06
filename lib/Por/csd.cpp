#include "por/cone.h"
#include "por/csd.h"
#include "por/thread_id.h"
#include "por/event/event.h"

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <deque>
#include <map>
#include <vector>

namespace por {
	namespace {
		using por::event::event;
		using por::event::thread_join;
		using por::event::thread_init;
		using por::event::lock_acquire;
		using por::event::wait1;
		using por::event::wait2;
		using por::event::signal;
		using por::event::broadcast;
		using por::event::event_kind;
		using depth_t = por::event::event::depth_t;

		inline auto compute_thread_count(event const& local_configuration) {
			if(local_configuration.kind() == event_kind::thread_init) {
				// The cone does not contain the maximal event of the local_configuration.
				// If that element is a `thread_init` event, this means that its thread is not
				// represented in the cone at all, as it does not have any predecessors on the same thread.
				return local_configuration.cone().size() + 1;
			} else {
				return local_configuration.cone().size();
			}
		}

		inline auto collect(event const* ev) {
			std::map<thread_id, std::deque<event const*>> events;

			events[ev->tid()].emplace_front(ev);
			if(ev->kind() == event_kind::thread_init) {
				event const* cause = static_cast<por::event::thread_init const*>(ev)->thread_creation_predecessor();
				assert(cause->kind() == event_kind::thread_create || cause->kind() == event_kind::program_init);
				assert((cause->kind() == event_kind::program_init ? ev->tid() == thread_id(thread_id{}, 1) : true)
					&& "The initial thread always has the tid (1).");
			}

			for(auto const& pair : ev->cone()) {
				auto& vec = events[pair.first];
				for(event const* ev = pair.second; ev ; ev = ev->thread_predecessor()) {
					vec.emplace_front(ev);
				}
				assert(vec.front()->kind() == event_kind::thread_init);
				event const* cause = static_cast<por::event::thread_init const*>(vec.front())->thread_creation_predecessor();
				assert(cause->kind() == event_kind::thread_create || cause->kind() == event_kind::program_init);
				assert((cause->kind() == event_kind::program_init ? vec.front()->tid() == thread_id(thread_id{}, 1) : true)
					&& "The initial thread always has the tid (1).");
			}

			assert(!events.empty() && "At least one thread (the initial thread) must exist in the local configuration");
			assert(events.begin()->first == thread_id(thread_id{}, 1) && "The initial thread must exist");
			return events;
		}

		bool has_run(
			std::map<thread_id, std::pair<std::deque<event const*> const*, std::size_t>> const& advancement,
			event const* ev
		) {
			auto const& [thread_events, thread_advancement] = advancement.at(ev->tid());
			return thread_advancement > 0 && (*thread_events)[thread_advancement - 1]->depth() >= ev->depth();
		}

		bool event_is_enabled(
			std::map<thread_id, std::pair<std::deque<event const*> const*, std::size_t>> const& advancement,
			event const* ev
		) {
			libpor_check(ev == advancement.at(ev->tid()).first->at(advancement.at(ev->tid()).second));
			switch(ev->kind()) {
				case event_kind::local: return true;
				case event_kind::program_init: {
					libpor_check(false && "program_init events should never be checked for enabled-ness");
					return true;
				} break;
				case event_kind::thread_create: return true;
				case event_kind::thread_join: {
					auto* joined_pred = static_cast<thread_join const*>(ev)->joined_thread_predecessor();
					return has_run(advancement, joined_pred);
				} break;
				case event_kind::thread_init: {
					auto* creation_pred = static_cast<thread_init const*>(ev)->thread_creation_predecessor();
					return creation_pred->kind() == event_kind::program_init || has_run(advancement, creation_pred);
				} break;
				case event_kind::thread_exit: return true;
				case event_kind::lock_create: return true;
				case event_kind::lock_destroy: return true; // race otherwise
				case event_kind::lock_acquire: {
					auto* lock_pred = static_cast<lock_acquire const*>(ev)->lock_predecessor();
					return !lock_pred || lock_pred->tid() == ev->tid() || has_run(advancement, lock_pred);
				} break;
				case event_kind::lock_release: return true; // lock is always owned by the releasing thread
				case event_kind::condition_variable_create: return true;
				case event_kind::condition_variable_destroy: return true; // race otherwise
				case event_kind::wait1: {
					// lock is always owned by the releasing thread
					auto cond_preds = static_cast<wait1 const*>(ev)->condition_variable_predecessors();
					return std::all_of(cond_preds.begin(), cond_preds.end(), [&advancement](event const* ev) {
						return has_run(advancement, ev);
					});
				} break;
				case event_kind::wait2: {
					// the notifying_predecessor is always on another thread and is always nonnull
					if(!has_run(advancement, static_cast<wait2 const*>(ev)->notifying_predecessor())) {
						return false;
					}
					auto* lock_pred = static_cast<wait2 const*>(ev)->lock_predecessor();
					// wait2 locks must always exist, as they must have been previously released by the wait1
					return lock_pred->tid() == ev->tid() || has_run(advancement, lock_pred);
				} break;
				case event_kind::signal: {
					auto cond_preds = static_cast<signal const*>(ev)->condition_variable_predecessors();
					return std::all_of(cond_preds.begin(), cond_preds.end(), [&advancement](event const* ev) {
						return has_run(advancement, ev);
					});
				} break;
				case event_kind::broadcast: {
					auto cond_preds = static_cast<broadcast const*>(ev)->condition_variable_predecessors();
					return std::all_of(cond_preds.begin(), cond_preds.end(), [&advancement](event const* ev) {
						return has_run(advancement, ev);
					});
				} break;
			}
			assert(false && "unreachable");
			std::abort();
		}

		inline void advance_current_thread(
			std::map<thread_id, std::pair<std::deque<event const*> const*, std::size_t>> const& advancement,
			std::pair<std::deque<event const*> const*, std::size_t>& self_advancement,
			thread_id const& current_thread
		) {
			event const* ev = (*self_advancement.first)[self_advancement.second];
			assert(event_is_enabled(advancement, ev));
			do {
				++self_advancement.second;
				if(self_advancement.second >= self_advancement.first->size()) {
					return;
				}
				ev = (*self_advancement.first)[self_advancement.second];
			} while(event_is_enabled(advancement, ev));
		}

		bool csd_limit_search(
			std::map<thread_id, std::pair<std::deque<event const*> const*, std::size_t>> advancement,
			thread_id const& current_thread,
			csd_t const current_csd,
			csd_t const csd_limit
		) {
			auto& self_advancement = advancement.at(current_thread);
			// step 1: advance current thread as far as possible
			advance_current_thread(advancement, self_advancement, current_thread);

			// step 2: check if we are done
			if(self_advancement.second >= self_advancement.first->size()) {
				bool done = true;
				for(auto const& p : advancement) {
					if(p.second.second < p.second.first->size()) {
						done = false;
						break;
					}
				}
				if(done) {
					return false; // we are not above the csd limit
				}
			}

			if(current_csd + 1 > csd_limit) {
				return true; // all possible extensions are above the csd limit
			}

			// step 3: recurse by checking alternatives
			#ifndef NDEBUG
			bool advancement_possible = false;
			#endif
			for(auto const& p : advancement) {
				if(p.first != current_thread && p.second.second < p.second.first->size()) {
					if(event_is_enabled(advancement, p.second.first->at(p.second.second))) {
						#ifndef NDEBUG
						advancement_possible = true;
						#endif
						if(false == csd_limit_search(advancement, p.first, current_csd + 1, csd_limit)) {
							// we have found a possible execution that remains below the csd limit
							return false;
						}
					}
				}
			}
			assert(advancement_possible && "In a non-finished search, advancement must be possible");

			// all alternatives lead to exceeding the csd limit
			return true;
		}

		csd_t csd_search(
			std::map<thread_id, std::pair<std::deque<event const*> const*, std::size_t>> advancement,
			thread_id const& current_thread,
			csd_t csd_budget
		) {
			auto& self_advancement = advancement.at(current_thread);
			// step 1: advance current thread as far as possible
			advance_current_thread(advancement, self_advancement, current_thread);

			// step 2: check if we are done
			csd_t active_threads = 0;
			for(auto const& p : advancement) {
				if(p.second.second < p.second.first->size()) {
					++active_threads;
				}
			}
			if(active_threads == 0) {
				return 0; // no additional context switches are needed
			} else if(active_threads > csd_budget) {
			 // the current thread is blocked, so we need at leaast `k` context switches
			 // to visit `k` remaining threads (if the current thread is still active we
			 // will have to revisit it once its current blocker is resolved)
				return csd_budget + 1;
			}

			// step 3: recurse by checking alternatives
			csd_t csd = csd_budget + 1;
			#ifndef NDEBUG
			bool has_enabled_events = false;
			#endif
			for(auto const& p : advancement) {
				if(p.first != current_thread && p.second.second < p.second.first->size()) {
					if(event_is_enabled(advancement, p.second.first->at(p.second.second))) {
						#ifndef NDEBUG
						has_enabled_events = true;
						#endif
						// The next step has a limit that is *two* below our current best solution,
						// as we will need to increment it by one due to the context switch that we
						// are currently trying to choose, and still want to find an improvement
						// (of at least one) over the current best solution (tying with the best is
						// a waste of time, since we are just checking existence).
						// Note that the initial "best" is one above the limit, meaning that we will
						// begin looking for a context switch that will just fulfill the limit (be
						// one the value that is one above the limit)
						auto next_csd = csd_search(advancement, p.first, csd - 2) + 1;
						if(next_csd <= 1) {
							assert(csd > 0 && csd <= csd_budget + 1);
							return next_csd;
						} else if(next_csd < csd) {
							csd = next_csd;
						}
					}
				}
			}
			assert(has_enabled_events);
			assert(csd > 0 && csd <= csd_budget + 1);
			return csd;
		}
	}

	bool is_above_csd_limit_1(por::event::event const& local_configuration, csd_t limit) {
		using por::event::event;

		if(auto thread_count = compute_thread_count(local_configuration); thread_count <= 1) {
			assert(thread_count != 0 && "`is_above_csd_limit` should only be called if threads exist");
			return false;
		} else if(thread_count - 1 > limit) {
			return true;
		}

		auto events = collect(&local_configuration);
		// the actual computation will use pointers to the deques in the `events` map
		std::map<thread_id, std::pair<std::deque<event const*> const*, std::size_t>> initial_advancement;
		for(auto const& p : events) {
			initial_advancement.emplace_hint(initial_advancement.end(), p.first, std::make_pair<std::deque<event const*> const*, std::size_t>(&p.second, 0));
		}

		return csd_limit_search(initial_advancement, events.begin()->first, 0, limit);
	}

	csd_t compute_csd_1(por::event::event const& local_configuration) {
		using por::event::event;

		if(auto const thread_count = compute_thread_count(local_configuration); thread_count <= 1) {
			assert(thread_count != 0 && "`compute_csd` should only be called if threads exist");
			return 0;
		}

		auto events = collect(&local_configuration);
		// the actual computation will use pointers to the deques in the `events` map
		std::map<thread_id, std::pair<std::deque<event const*> const*, std::size_t>> initial_advancement;
		for(auto const& p : events) {
			initial_advancement.emplace_hint(initial_advancement.end(), p.first, std::make_pair<std::deque<event const*> const*, std::size_t>(&p.second, 0));
		}

		return csd_search(initial_advancement, events.begin()->first, (std::numeric_limits<csd_t>::max)() - 1);
	}
}
