#include "por/cone.h"
#include "por/csd.h"
#include "por/thread_id.h"
#include "por/event/event.h"

#include <algorithm>
#include <deque>
#include <map>
#include <vector>

namespace por {
	namespace {
		using por::event::event;
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

		bool event_is_enabled(
			std::map<thread_id, std::pair<std::deque<event const*> const*, std::size_t>> const& advancement,
			event const* ev
		) {
			libpor_check(ev == advancement.at(ev->tid()).first->at(advancement.at(ev->tid()).second));
			for(event const* pred : ev->predecessors()) {
				assert(!!pred);
				if(pred->tid() != ev->tid() && pred->kind() != event_kind::program_init) {
					auto const& pred_advancement = advancement.at(pred->tid());
					if(pred_advancement.second <= 0) {
						return false;
					}
					if((*pred_advancement.first)[pred_advancement.second - 1]->depth() < pred->depth()) {
						return false;
					}
				}
			}
			return true;
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
			csd_t csd_limit
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
			} else if(active_threads > csd_limit) {
			 // the current thread is blocked, so we need at leaast `k` context switches
			 // to visit `k` remaining threads (if the current thread is still active we
			 // will have to revisit it once its current blocker is resolved)
				return csd_limit + 1;
			}

			// step 3: recurse by checking alternatives
			csd_t csd = (std::numeric_limits<csd_t>::max)();
			for(auto const& p : advancement) {
				if(p.first != current_thread && p.second.second < p.second.first->size()) {
					if(event_is_enabled(advancement, p.second.first->at(p.second.second))) {
						auto next_csd = csd_search(advancement, p.first, csd_limit - 1) + 1;
						if(next_csd < csd) {
							csd = next_csd;
						}
					}
				}
			}
			assert(csd > 0 && csd <= csd_limit);
			return csd;
		}
	}

	bool is_above_csd_limit_2(por::event::event const& local_configuration, csd_t limit) {
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

	csd_t compute_csd_2(por::event::event const& local_configuration) {
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
