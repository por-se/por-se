#include "por/cone.h"
#include "por/csd.h"
#include "por/thread_id.h"
#include "por/event/event.h"
#include "util/check.h"
#include "util/at_scope_exit.h"

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
		using por::event::lock_release;
		using por::event::wait1;
		using por::event::wait2;
		using por::event::signal;
		using por::event::broadcast;
		using por::event::event_kind;
		using depth_t = por::event::event::depth_t;
		using por::event::lock_id_t;

		class csd_search_t {
			std::map<thread_id, std::pair<std::vector<event const*> const, std::size_t>> advancement;
			std::map<lock_id_t, bool> locked;

			enum class enabled_t {
				ENABLED,
				PREEMPTING_DISABLED,
				NONPREEMPTING_DISABLED,
			};

			inline bool has_run(event const* ev) {
				auto const& [thread_events, thread_advancement] = advancement.at(ev->tid());
				return thread_advancement < thread_events.size() && thread_events[thread_advancement]->depth() >= ev->depth();
			}

			enabled_t event_preemption(event const* ev) {
				libpor_check(ev == advancement.at(ev->tid()).first.at(advancement.at(ev->tid()).second - 1));
				switch(ev->kind()) {
					case event_kind::local: return enabled_t::ENABLED;
					case event_kind::program_init: {
						libpor_check(false && "program_init events should never be checked for enabled-ness");
						return enabled_t::ENABLED;
					} break;
					case event_kind::thread_create: return enabled_t::ENABLED;
					case event_kind::thread_join: {
						auto* joined_pred = static_cast<thread_join const*>(ev)->joined_thread_predecessor();
						if(has_run(joined_pred)) {
							return enabled_t::ENABLED;
						} else {
							return enabled_t::NONPREEMPTING_DISABLED;
						}
					} break;
					case event_kind::thread_init: {
						auto* creation_pred = static_cast<thread_init const*>(ev)->thread_creation_predecessor();
						if(creation_pred->kind() == event_kind::program_init || has_run(creation_pred)) {
							return enabled_t::ENABLED;
						} else {
							return enabled_t::NONPREEMPTING_DISABLED;
						}
					} break;
					case event_kind::thread_exit: return enabled_t::ENABLED;
					case event_kind::lock_create: return enabled_t::ENABLED;
					case event_kind::lock_destroy: return enabled_t::ENABLED; // race otherwise
					case event_kind::lock_acquire: {
						auto* lock_pred = static_cast<lock_acquire const*>(ev)->lock_predecessor();
						if(!lock_pred || lock_pred->tid() == ev->tid() || has_run(lock_pred)) {
							return enabled_t::ENABLED;
						} else if(locked.at(static_cast<lock_acquire const*>(ev)->lid())) {
							return enabled_t::NONPREEMPTING_DISABLED;
						} else {
							return enabled_t::PREEMPTING_DISABLED;
						}
					} break;
					case event_kind::lock_release: return enabled_t::ENABLED; // lock is always owned by the releasing thread
					case event_kind::condition_variable_create: return enabled_t::ENABLED;
					case event_kind::condition_variable_destroy: return enabled_t::ENABLED; // race otherwise
					case event_kind::wait1: {
						// lock is always owned by the releasing thread
						auto cond_preds = static_cast<wait1 const*>(ev)->condition_variable_predecessors();
						if(std::all_of(cond_preds.begin(), cond_preds.end(), [this](event const* ev) {
							return has_run(ev);
						})) {
							return enabled_t::ENABLED;
						} else {
							return enabled_t::PREEMPTING_DISABLED;
						}
					} break;
					case event_kind::wait2: {
						// the notifying_predecessor is always on another thread and is always nonnull
						if(!has_run(static_cast<wait2 const*>(ev)->notifying_predecessor())) {
							return enabled_t::NONPREEMPTING_DISABLED;
						}
						auto* lock_pred = static_cast<wait2 const*>(ev)->lock_predecessor();
						// wait2 locks must always exist, as they must have been previously released by the wait1
						if(lock_pred->tid() == ev->tid() || has_run(lock_pred)) {
							return enabled_t::ENABLED;
						} else if(locked.at(static_cast<wait2 const*>(ev)->lid())) {
							return enabled_t::NONPREEMPTING_DISABLED;
						} else {
							return enabled_t::PREEMPTING_DISABLED;
						}
					} break;
					case event_kind::signal: {
						auto cond_preds = static_cast<signal const*>(ev)->condition_variable_predecessors();
						if(std::all_of(cond_preds.begin(), cond_preds.end(), [this](event const* ev) {
							return has_run(ev);
						})) {
							return enabled_t::ENABLED;
						} else {
							return enabled_t::PREEMPTING_DISABLED;
						}
					} break;
					case event_kind::broadcast: {
						auto cond_preds = static_cast<broadcast const*>(ev)->condition_variable_predecessors();
						if(std::all_of(cond_preds.begin(), cond_preds.end(), [this](event const* ev) {
							return has_run(ev);
						})) {
							return enabled_t::ENABLED;
						} else {
							return enabled_t::PREEMPTING_DISABLED;
						}
					} break;
				}
				assert(false && "unreachable");
				std::abort();
			}

			bool event_is_enabled(event const* ev) {
				libpor_check(ev == advancement.at(ev->tid()).first.at(advancement.at(ev->tid()).second - 1));
				switch(ev->kind()) {
					case event_kind::local: return true;
					case event_kind::program_init: {
						libpor_check(false && "program_init events should never be checked for enabled-ness");
						return true;
					} break;
					case event_kind::thread_create: return true;
					case event_kind::thread_join: {
						auto* joined_pred = static_cast<thread_join const*>(ev)->joined_thread_predecessor();
						return has_run(joined_pred);
					} break;
					case event_kind::thread_init: {
						auto* creation_pred = static_cast<thread_init const*>(ev)->thread_creation_predecessor();
						return creation_pred->kind() == event_kind::program_init || has_run(creation_pred);
					} break;
					case event_kind::thread_exit: return true;
					case event_kind::lock_create: return true;
					case event_kind::lock_destroy: return true; // race otherwise
					case event_kind::lock_acquire: {
						auto* lock_pred = static_cast<lock_acquire const*>(ev)->lock_predecessor();
						return !lock_pred || lock_pred->tid() == ev->tid() || has_run(lock_pred);
					} break;
					case event_kind::lock_release: return true; // lock is always owned by the releasing thread
					case event_kind::condition_variable_create: return true;
					case event_kind::condition_variable_destroy: return true; // race otherwise
					case event_kind::wait1: {
						// lock is always owned by the releasing thread
						auto cond_preds = static_cast<wait1 const*>(ev)->condition_variable_predecessors();
						return std::all_of(cond_preds.begin(), cond_preds.end(), [this](event const* ev) {
							return has_run(ev);
						});
					} break;
					case event_kind::wait2: {
						// the notifying_predecessor is always on another thread and is always nonnull
						if(!has_run(static_cast<wait2 const*>(ev)->notifying_predecessor())) {
							return false;
						}
						// wait2 locks must always exist, as they must have been previously released by the wait1
						auto* lock_pred = static_cast<wait2 const*>(ev)->lock_predecessor();
						return lock_pred->tid() == ev->tid() || has_run(lock_pred);
					} break;
					case event_kind::signal: {
						auto cond_preds = static_cast<signal const*>(ev)->condition_variable_predecessors();
						return std::all_of(cond_preds.begin(), cond_preds.end(), [this](event const* ev) {
							return has_run(ev);
						});
					} break;
					case event_kind::broadcast: {
						auto cond_preds = static_cast<broadcast const*>(ev)->condition_variable_predecessors();
						return std::all_of(cond_preds.begin(), cond_preds.end(), [this](event const* ev) {
							return has_run(ev);
						});
					} break;
				}
				assert(false && "unreachable");
				std::abort();
			}

			// returns the cost (in number of preemptive context switches) to advance beyond the current thread
			// the cost can only be 0 or 1
			inline unsigned advance_thread(std::pair<std::vector<event const*> const, std::size_t>& self_advancement) {
				assert(self_advancement.second > 0);
				assert(event_preemption(self_advancement.first.at(self_advancement.second - 1)) == enabled_t::ENABLED);
				for(;;) {
					--self_advancement.second;
					if(self_advancement.second == 0) {
						return 0;
					}
					auto const* ev = self_advancement.first[self_advancement.second - 1];
					auto enabled = event_preemption(ev);
					if (enabled == enabled_t::NONPREEMPTING_DISABLED) {
						return 0;
					}
					if (enabled == enabled_t::PREEMPTING_DISABLED) {
						return 1;
					}
					assert(enabled == enabled_t::ENABLED);
					auto kind = ev->kind();
					if (kind == event_kind::lock_acquire) {
						libpor_check(locked.at(static_cast<lock_acquire const*>(ev)->lid()) == false);
						locked.at(static_cast<lock_acquire const*>(ev)->lid()) = true;
					} else if (kind == event_kind::lock_release) {
						libpor_check(locked.at(static_cast<lock_release const*>(ev)->lid()) == true);
						locked.at(static_cast<lock_release const*>(ev)->lid()) = false;
					} else if (kind == event_kind::wait1) {
						libpor_check(locked.at(static_cast<wait1 const*>(ev)->lid()) == true);
						locked.at(static_cast<wait1 const*>(ev)->lid()) = false;
					} else if (kind == event_kind::wait2) {
						libpor_check(locked.at(static_cast<wait2 const*>(ev)->lid()) == false);
						locked.at(static_cast<wait2 const*>(ev)->lid()) = true;
					}
				}
			}

			inline void revert_thread(std::pair<std::vector<event const*> const, std::size_t>& self_advancement, std::size_t const to) {
				assert(self_advancement.second < to && to <= self_advancement.first.size());
				do {
					auto const* ev = self_advancement.first[self_advancement.second];
					auto kind = ev->kind();
					if (kind == event_kind::lock_acquire) {
						libpor_check(locked.at(static_cast<lock_acquire const*>(ev)->lid()) == true);
						locked.at(static_cast<lock_acquire const*>(ev)->lid()) = false;
					} else if (kind == event_kind::lock_release) {
						libpor_check(locked.at(static_cast<lock_release const*>(ev)->lid()) == false);
						locked.at(static_cast<lock_release const*>(ev)->lid()) = true;
					} else if (kind == event_kind::wait1) {
						libpor_check(locked.at(static_cast<wait1 const*>(ev)->lid()) == false);
						locked.at(static_cast<wait1 const*>(ev)->lid()) = true;
					} else if (kind == event_kind::wait2) {
						libpor_check(locked.at(static_cast<wait2 const*>(ev)->lid()) == true);
						locked.at(static_cast<wait2 const*>(ev)->lid()) = false;
					}
					++self_advancement.second;
				} while(self_advancement.second < to);
			}

			bool _is_above(
				std::pair<std::vector<event const*> const, std::size_t>& self_advancement,
				csd_t const current_csd,
				csd_t const csd_limit
			) {
				auto self_advancement_cleanup = util::make_at_scope_exit(
					[this, &self_advancement, previous_advancement = self_advancement.second]() {
						revert_thread(self_advancement, previous_advancement);
					}
				);
				// step 1: advance current thread as far as possible
				auto csd_step = advance_thread(self_advancement);

				// step 2: check if we are done
				if(std::all_of(advancement.cbegin(), advancement.cend(),
					[](auto const& p) { return p.second.second == 0; })
				) {
					return false; // we are not above the csd limit
				} else if(current_csd + csd_step > csd_limit) {
					return true; // all possible extensions are above the csd limit
				}

				// step 3: recurse by checking alternatives
				#ifndef NDEBUG
				bool advancement_possible = false;
				#endif
				for(auto& [tid, thread_advancement] : advancement) {
					if(&thread_advancement != &self_advancement && thread_advancement.second > 0) {
						if(event_is_enabled(thread_advancement.first[thread_advancement.second - 1])) {
							#ifndef NDEBUG
							advancement_possible = true;
							#endif
							if(false == _is_above(thread_advancement, current_csd + csd_step, csd_limit)) {
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

			csd_t _compute(
				std::pair<std::vector<event const*> const, std::size_t>& self_advancement,
				csd_t const csd_budget
			) {
				auto self_advancement_cleanup = util::make_at_scope_exit(
					[this, &self_advancement, previous_advancement = self_advancement.second]() {
						revert_thread(self_advancement, previous_advancement);
					}
				);
				// step 1: advance current thread as far as possible
				auto csd_step = advance_thread(self_advancement);

				// step 2: check if we are done
				if(std::all_of(advancement.cbegin(), advancement.cend(),
					[](auto const& p) { return p.second.second == 0; })
				) {
					return 0; // no further context switches are needed
				} else if(csd_step > csd_budget) {
					return csd_budget + 1; // all possible extensions are worse than a previous attempt
				}

				// step 3: recurse by checking alternatives
				csd_t csd = csd_budget + 1;
				#ifndef NDEBUG
				bool has_enabled_events = false;
				#endif
				for(auto& [tid, thread_advancement] : advancement) {
					if(&thread_advancement != &self_advancement && thread_advancement.second > 0) {
						if(event_is_enabled(thread_advancement.first[thread_advancement.second - 1])) {
							#ifndef NDEBUG
							has_enabled_events = true;
							#endif
							// The next step has a limit that is *one or two* below our current best
							// solution, as we will need to increment it by *up to one* due to the context
							// switch that we are currently trying to choose, and still want to find an
							// improvement (of at least one) over the current best solution (tying with
							// the best is a waste of time, since we are just checking existence).
							// Note that the initial "best" is one above the limit, meaning that we will
							// begin looking for a context switch that will just fulfill the limit (be
							// one the value that is one above the limit)
							auto next_csd = _compute(thread_advancement, csd - 1 - csd_step) + csd_step;
							if(next_csd <= csd_step) {
								assert(csd >= csd_step && csd <= csd_budget + 1);
								return next_csd;
							} else if(next_csd < csd) {
								csd = next_csd;
							}
						}
					}
				}
				assert(has_enabled_events);
				assert(csd >= csd_step && csd <= csd_budget + 1);
				return csd;
			}

			static bool may_be_blocking(event const* ev) noexcept {
				auto kind = ev->kind();
				if(kind == event_kind::lock_acquire) {
					event const* lock_pred = static_cast<lock_acquire const*>(ev)->lock_predecessor();
					return !!lock_pred && !lock_pred->is_less_than_eq(*static_cast<lock_acquire const*>(ev)->thread_predecessor());
				} else if(kind == event_kind::thread_init) {
					return true; // is blocked by thread creation
				} else if(kind == event_kind::thread_join) {
					return true; // exit is only depended upon by join
				} else if(kind == event_kind::wait1) {
					auto cond_preds = static_cast<wait1 const*>(ev)->condition_variable_predecessors();
					if(cond_preds.empty()) {
						return false;
					}
					auto const* thread_pred = static_cast<wait1 const*>(ev)->thread_predecessor();
					return std::any_of(cond_preds.begin(), cond_preds.end(), [thread_pred](auto const* cond_pred) {
						return !cond_pred->is_less_than_eq(*thread_pred);
					});
				} else if(kind == event_kind::wait2) {
					return true; // the notifying_predecessor must always be on another thread
				} else if(kind == event_kind::signal) {
					auto cond_preds = static_cast<signal const*>(ev)->condition_variable_predecessors();
					if(cond_preds.empty()) {
						return false;
					}
					auto const* thread_pred = static_cast<signal const*>(ev)->thread_predecessor();
					return std::any_of(cond_preds.begin(), cond_preds.end(), [thread_pred](auto const* cond_pred) {
						return !cond_pred->is_less_than_eq(*thread_pred);
					});
				} else if(kind == event_kind::broadcast) {
					auto cond_preds = static_cast<broadcast const*>(ev)->condition_variable_predecessors();
					if(cond_preds.empty()) {
						return false;
					}
					auto const* thread_pred = static_cast<broadcast const*>(ev)->thread_predecessor();
					return std::any_of(cond_preds.begin(), cond_preds.end(), [thread_pred](auto const* cond_pred) {
						return !cond_pred->is_less_than_eq(*thread_pred);
					});
				} else {
					return false;
				}
			}

		public:
			csd_search_t(event const* local_configuration) {
				// Some special-casing will because the `local_configuration` event itself is not part of the cone.
				auto const& cone = local_configuration->cone();
				auto thread_count = local_configuration->kind() == event_kind::thread_init ? cone.size() + 1 : cone.size();

				if(thread_count <= 1) {
					assert(thread_count > 0 && "The csd should only be checked or computed if threads exist");
					return;
				}

				if(local_configuration->kind() == event_kind::thread_init) {
					advancement.emplace(local_configuration->tid(),
						std::make_pair<std::vector<event const*> const, std::size_t>({local_configuration}, 1)
					);
				}

				for(auto const& [tid, cone_event] : cone) {
					std::vector<event const*> vec;
					if(local_configuration->tid() == tid) {
						auto const kind = local_configuration->kind();
						assert(kind != event_kind::thread_init);
						if (kind == event_kind::lock_acquire) {
							locked.try_emplace(static_cast<lock_acquire const*>(local_configuration)->lid(), false);
						} else if (kind == event_kind::lock_release) {
							locked.try_emplace(static_cast<lock_release const*>(local_configuration)->lid(), false);
						} else if (kind == event_kind::wait1) {
							locked.try_emplace(static_cast<wait1 const*>(local_configuration)->lid(), false);
						} else if (kind == event_kind::wait2) {
							locked.try_emplace(static_cast<wait2 const*>(local_configuration)->lid(), false);
						}
						vec.emplace_back(local_configuration);
					}
					for(event const* ev = cone_event; ev ; ev = ev->thread_predecessor()) {
						// we need to insert the following:
						// - all events which may be blocking other events
						// - all events which manipulate lock state (we assume creation and deletion to be handled correctly)
						// - *enough* events to ensure that it is possible to determine if a dependency `has_run`
						auto kind = ev->kind();
						if(kind == event_kind::local) {
							// The cone cannot contain a local event and intermediate local events
							// are of no importance, as they can never be the non-thread dependencies.
						} else if(kind == event_kind::thread_init) {
							vec.emplace_back(ev);
						} else if(kind == event_kind::thread_join) {
							vec.emplace_back(ev);
						} else if (kind == event_kind::lock_acquire) {
							locked.try_emplace(static_cast<lock_acquire const*>(ev)->lid(), false);
							vec.emplace_back(ev);
						} else if (kind == event_kind::lock_release) {
							locked.try_emplace(static_cast<lock_release const*>(ev)->lid(), false);
							vec.emplace_back(ev);
						} else if (kind == event_kind::wait1) {
							locked.try_emplace(static_cast<wait1 const*>(ev)->lid(), false);
							vec.emplace_back(ev);
						} else if (kind == event_kind::wait2) {
							locked.try_emplace(static_cast<wait2 const*>(ev)->lid(), false);
							vec.emplace_back(ev);
						} else {
							// Non-local events may be non-thread dependencies, even if they themselves
							// are not blocking. Therefore, they must be inserted if the *next* event
							// (which is the previously inserted event) can block execution. This way,
							// we can skip all nonblocking events in a row except for the last one -
							// whose depth is the largest of all those events, and can thus serve to
							// determine if a given event has run, no matter whether it is included in the
							// advancement or not.
							if(vec.empty() || may_be_blocking(vec.back())) {
								vec.emplace_back(ev);
							}
						}
					}
					assert(vec.back()->kind() == event_kind::thread_init);
					advancement.emplace(tid, std::make_pair<std::vector<event const*> const, std::size_t>(std::move(vec), vec.size()));
				}

				assert(!advancement.empty() && "At least one thread (the initial thread) must exist in the local configuration");
				assert(advancement.begin()->first == thread_id(thread_id{}, 1) && "The least element of the map must be the initial thread");
			}

			csd_t compute() {
				if(advancement.empty()) {
					return 0;
				}

				return _compute(advancement.begin()->second, (std::numeric_limits<csd_t>::max)() - 1);
			}

			bool is_above(csd_t const limit) {
				if(advancement.empty()) {
					return false;
				}

				return _is_above(advancement.begin()->second, 0, limit);
			}
		};
	}

	bool is_above_csd_limit_2(por::event::event const& local_configuration, csd_t limit) {
		return csd_search_t(&local_configuration).is_above(limit);
	}

	csd_t compute_csd_2(por::event::event const& local_configuration) {
		return csd_search_t(&local_configuration).compute();
	}
}
