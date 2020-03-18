#include "include/por/cone.h"
#include "include/por/csd.h"
#include "include/por/thread_id.h"
#include "include/por/event/event.h"

#include <deque>
#include <map>
#include <algorithm>

namespace por {
	namespace {
		using por::event::event;
		using depth_t = por::event::event::depth_t;
		using por::event::event_kind;

		inline auto compute_thread_count(event const& local_configuration) {
			if(local_configuration.kind() == event_kind::thread_init) {
				return local_configuration.cone().size() + 1;
			} else {
				return local_configuration.cone().size();
			}
		}

		inline auto collect(event const* ev) {
			std::map<thread_id, std::deque<event const*>> events;
			thread_id initial_thread;

			events[ev->tid()].emplace_front(ev);
			if(ev->kind() == event_kind::thread_init) {
				event const* cause = static_cast<por::event::thread_init const*>(ev)->thread_creation_predecessor();
				assert(cause->kind() == event_kind::thread_create || cause->kind() == event_kind::program_init);
				if(cause->kind() == event_kind::program_init) {
					assert(initial_thread == thread_id{} && "Only one initial thread is supported");
					initial_thread = ev->tid();
				}
			}

			for(auto const& pair : ev->cone()) {
				auto& vec = events[pair.first];
				for(event const* ev = pair.second; ev ; ev = ev->thread_predecessor()) {
					vec.emplace_front(ev);
				}
				assert(vec.front()->kind() == event_kind::thread_init);
				event const* cause = static_cast<por::event::thread_init const*>(vec.front())->thread_creation_predecessor();
				assert(cause->kind() == event_kind::thread_create || cause->kind() == event_kind::program_init);
				if(cause->kind() == event_kind::program_init) {
					assert(initial_thread == thread_id{} && "Only one initial thread is supported");
					initial_thread = vec.front()->tid();
				}
			}
			assert(initial_thread != thread_id{} && "One initial thread is required");

			return std::make_pair(events, initial_thread);
		}

		bool event_is_enabled(
			std::map<thread_id, std::pair<std::deque<event const*> const*, std::size_t>> const& advancement,
			event const* ev
		) {
			assert(ev == advancement.at(ev->tid()).first->at(advancement.at(ev->tid()).second));
			for(event const* pred : ev->predecessors()) {
				assert(!!pred);
				if(pred->tid() != ev->tid() && pred->kind() != event_kind::program_init) {
					auto const& pred_advancement = advancement.at(pred->tid());
					if(pred_advancement.second <= 0) {
						return false;
					}
					if(pred_advancement.first->at(pred_advancement.second - 1)->depth() < pred->depth()) {
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
			event const* ev = self_advancement.first->at(self_advancement.second);
			assert(event_is_enabled(advancement, ev));
			do {
				++self_advancement.second;
				if(self_advancement.second >= self_advancement.first->size()) {
					ev = nullptr;
					break;
				}
				ev = self_advancement.first->at(self_advancement.second);
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
	}

	bool is_above_csd_limit(por::event::event const& local_configuration, csd_t limit) {
		using por::event::event;
		using por::event::event_kind;

		if(auto thread_count = compute_thread_count(local_configuration); thread_count == 0) {
			return false;
		} else if(thread_count == 1) {
			return 1 > limit;
		} else if(thread_count > limit) {
			return true;
		}

		auto [events, initial_thread] = collect(&local_configuration);
		std::map<thread_id, std::pair<std::deque<event const*> const*, std::size_t>> initial_advancement;
		for(auto const& p : events) {
			initial_advancement.emplace(p.first, std::make_pair<std::deque<event const*> const*, std::size_t>(&p.second, 0));
		}

		return csd_limit_search(initial_advancement, initial_thread, 1, limit);
	}
}
