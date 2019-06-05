#pragma once

#include "event/event.h"
#include "unfolding.h"

#include <util/sso_array.h>

#include <cassert>
#include <map>
#include <set>
#include <vector>

namespace por {
	class configuration;
}

namespace klee {
	class ExecutionState;
	por::configuration* configuration_from_execution_state(klee::ExecutionState const* s);
}

namespace por {
	class conflicting_extension {
		std::shared_ptr<por::event::event> _new_event;
		util::sso_array<std::shared_ptr<por::event::event>, 1> _conflicting_events;

	public:
		conflicting_extension(std::shared_ptr<por::event::event> new_event, std::shared_ptr<por::event::event> conflicting_event)
		: _new_event(std::move(new_event))
		, _conflicting_events{util::create_uninitialized, 1ul}
		{
			assert(_new_event);
			// we perform a very small optimization by allocating the conflicts in uninitialized storage
			new(_conflicting_events.data() + 0) std::shared_ptr<por::event::event>(std::move(conflicting_event));
			assert(_conflicting_events[0]);
		}

		std::shared_ptr<por::event::event>& new_event() {
			return _new_event;
		}
		std::shared_ptr<por::event::event> const& new_event() const {
			return _new_event;
		}

		util::iterator_range<std::shared_ptr<por::event::event>*> conflicts() {
			return util::make_iterator_range<std::shared_ptr<por::event::event>*>(_conflicting_events.data(), _conflicting_events.data() + _conflicting_events.size());
		}
		util::iterator_range<std::shared_ptr<por::event::event> const*> conflicts() const {
			return util::make_iterator_range<std::shared_ptr<por::event::event> const*>(_conflicting_events.data(), _conflicting_events.data() + _conflicting_events.size());
		}

		std::size_t num_of_conflicts() const {
			return _conflicting_events.size();
		}
	};

	class configuration_root {
		friend class configuration;

		std::shared_ptr<por::event::program_init> _program_init = event::program_init::alloc();

		std::shared_ptr<por::unfolding> _unfolding = std::make_shared<por::unfolding>(_program_init.get());

		std::map<por::event::thread_id_t, std::shared_ptr<event::event>> _thread_heads;
		por::event::thread_id_t _next_thread = 1;

	public:
		configuration construct();

		configuration_root& add_thread() {
			auto const tid = _next_thread++;
			assert(tid > 0);

			_thread_heads.emplace(tid, event::thread_init::alloc(tid, _program_init));
			_unfolding->mark_as_visited(_thread_heads[tid].get());

			return *this;
		}
	};

	class configuration {
		// creation events for locks and condition variables are optional
		static const bool optional_creation_events = true;

		// the unfolding this configuration is part of
		std::shared_ptr<por::unfolding> _unfolding;

		// contains most recent event of ALL threads that ever existed within this configuration
		std::map<por::event::thread_id_t, std::shared_ptr<event::event>> _thread_heads;

		// contains most recent event of ACTIVE locks
		std::map<event::lock_id_t, std::shared_ptr<event::event>> _lock_heads;

		// contains most recent event of ACTIVE condition variables for each thread
		std::map<por::event::cond_id_t, std::vector<std::shared_ptr<por::event::event>>> _cond_heads;

		// sequence of events in order of their execution
		std::vector<std::shared_ptr<por::event::event>> _schedule;

		// index of upcoming event, if _schedule_pos < _schedule.size(), catch-up is needed
		std::size_t _schedule_pos = 0;

		// sequence of standby execution states along the schedule
		std::vector<klee::ExecutionState const*> _standby_states;

	public:
		configuration() : configuration(configuration_root{}.add_thread().construct()) { }
		configuration(configuration const&) = default;
		configuration& operator=(configuration const&) = delete;
		configuration(configuration&&) = default;
		configuration& operator=(configuration&&) = default;
		configuration(configuration_root&& root)
			: _unfolding(std::move(root._unfolding))
			, _thread_heads(std::move(root._thread_heads))
		{
			_schedule.emplace_back(root._program_init);
			for(auto& thread : _thread_heads) {
				_schedule.emplace_back(thread.second);
			}
			_schedule_pos = _schedule.size();
			assert(!_thread_heads.empty() && "Cannot create a configuration without any startup threads");
		}
		~configuration() = default;

		auto const& thread_heads() const noexcept { return _thread_heads; }
		auto const& lock_heads() const noexcept { return _lock_heads; }
		auto const& cond_heads() const noexcept { return _cond_heads; }

		// `previous` is the maximal configuration that was used to generate the conflict
		configuration(configuration const& previous, conflicting_extension const& cex)
			: _unfolding(previous._unfolding)
			, _schedule(previous._schedule)
			, _standby_states(previous._standby_states)
		{
			// create new schedule and compute common prefix,
			// set _schedule_pos to latest theoretically possible standby
			_schedule_pos = compute_new_schedule_from_old(cex, _schedule) - 1;
			assert(_schedule_pos < _schedule.size());
			assert(_schedule.size() >= 2 && "there have to be at least program_init and thread_init");

			// find latest available standby state
			while(_schedule_pos > 0 && _standby_states[_schedule_pos] == nullptr) {
				--_schedule_pos;
			}
			assert(_schedule_pos >= 1 && "thread_init of main thread must always have a standby state");

			// remove standby states that are invalid in the new schedule
			_standby_states.erase(std::next(_standby_states.begin(), _schedule_pos + 1), _standby_states.end());
			assert(_standby_states.back() != nullptr);

			// no need to catch up to standby state
			++_schedule_pos;

			// reset heads to values at standby
			configuration* partial = configuration_from_execution_state(_standby_states.back());
			assert(partial != nullptr);
			_thread_heads = partial->thread_heads();
			_cond_heads = partial->cond_heads();
			_lock_heads = partial->lock_heads();
		}

		auto const& schedule() const noexcept { return _schedule; }
		bool needs_catch_up() const noexcept { return _schedule_pos < _schedule.size(); }

		por::event::event const* peek() const noexcept {
			if(!needs_catch_up()) {
				return nullptr;
			}
			return _schedule[_schedule_pos].get();
		}

		klee::ExecutionState const* standby_execution_state() const noexcept { return _standby_states.back(); }

		void standby_execution_state(klee::ExecutionState const* s) {
			assert(s != nullptr);
			assert(_schedule.size() > 0);
			assert(_schedule_pos > 0);
			_standby_states.resize(_schedule_pos, nullptr);
			assert(_standby_states.back() == nullptr);
			_standby_states.back() = s;
			assert(_standby_states.back() != nullptr);
		}

		por::event::thread_id_t active_threads() const noexcept {
			if(_thread_heads.size() == 0)
				return 0;
			por::event::thread_id_t res = 0;
			for(auto& e : _thread_heads) {
				assert(!!e.second);
				if(e.second->kind() != por::event::event_kind::thread_exit && e.second->kind() != por::event::event_kind::wait1)
					++res;
			}
			return res;
		}

		// Spawn a new thread from tid `source`.
		void spawn_thread(event::thread_id_t source, por::event::thread_id_t new_tid) {
			if(needs_catch_up()) {
				assert(_schedule[_schedule_pos]->kind() == por::event::event_kind::thread_create);
				assert(_schedule[_schedule_pos]->tid() == source);
				_thread_heads[source] = _schedule[_schedule_pos];
				++_schedule_pos;
				assert(_schedule[_schedule_pos]->kind() == por::event::event_kind::thread_init);
				assert(_schedule[_schedule_pos]->tid() == new_tid);
				_thread_heads[new_tid] = _schedule[_schedule_pos];
				++_schedule_pos;
				return;
			}

			auto source_it = _thread_heads.find(source);
			assert(source_it != _thread_heads.end() && "Source thread must exist");
			auto& source_event = source_it->second;
			assert(source_event->kind() != por::event::event_kind::thread_exit && "Source thread must not yet be exited");
			assert(source_event->kind() != por::event::event_kind::wait1 && "Source thread must not be blocked");

			source_event = event::thread_create::alloc(source, std::move(source_event));
			assert(new_tid > 0);
			assert(thread_heads().find(new_tid) == thread_heads().end() && "Thread with same id already exists");
			_thread_heads.emplace(new_tid, event::thread_init::alloc(new_tid, source_event));

			_schedule.emplace_back(source_event);
			_schedule.emplace_back(_thread_heads[new_tid]);
			_schedule_pos += 2;
			assert(_schedule_pos == _schedule.size());
		}

		void join_thread(event::thread_id_t thread, event::thread_id_t joined) {
			if(needs_catch_up()) {
				assert(_schedule[_schedule_pos]->kind() == por::event::event_kind::thread_join);
				assert(_schedule[_schedule_pos]->tid() == thread);
				_thread_heads[thread] = _schedule[_schedule_pos];
				++_schedule_pos;
				return;
			}

			auto thread_it = _thread_heads.find(thread);
			assert(thread_it != _thread_heads.end() && "Thread must exist");
			auto& thread_event = thread_it->second;
			assert(thread_event->kind() != por::event::event_kind::thread_exit && "Thread must not yet be exited");
			assert(thread_event->kind() != por::event::event_kind::wait1 && "Thread must not be blocked");
			auto joined_it = _thread_heads.find(joined);
			assert(joined_it != _thread_heads.end() && "Joined thread must exist");
			auto& joined_event = joined_it->second;
			assert(joined_event->kind() == por::event::event_kind::thread_exit && "Joined thread must be exited");

			thread_event = event::thread_join::alloc(thread, std::move(thread_event), joined_event);

			_schedule.emplace_back(thread_event);
			++_schedule_pos;
			assert(_schedule_pos == _schedule.size());
		}

		void exit_thread(event::thread_id_t thread) {
			if(needs_catch_up()) {
				assert(_schedule[_schedule_pos]->kind() == por::event::event_kind::thread_exit);
				assert(_schedule[_schedule_pos]->tid() == thread);
				_thread_heads[thread] = _schedule[_schedule_pos];
				++_schedule_pos;
				return;
			}

			auto thread_it = _thread_heads.find(thread);
			assert(thread_it != _thread_heads.end() && "Thread must exist");
			auto& thread_event = thread_it->second;
			assert(thread_event->kind() != por::event::event_kind::thread_exit && "Thread must not yet be exited");
			assert(thread_event->kind() != por::event::event_kind::wait1 && "Thread must not be blocked");

			assert(active_threads() > 0);
			thread_event = event::thread_exit::alloc(thread, std::move(thread_event));

			_schedule.emplace_back(thread_event);
			++_schedule_pos;
			assert(_schedule_pos == _schedule.size());
		}

		void create_lock(event::thread_id_t thread, event::lock_id_t lock) {
			if(needs_catch_up()) {
				assert(_schedule[_schedule_pos]->kind() == por::event::event_kind::lock_create);
				assert(_schedule[_schedule_pos]->tid() == thread);
				_thread_heads[thread] = _schedule[_schedule_pos];
				_lock_heads.emplace(lock, _schedule[_schedule_pos]);
				++_schedule_pos;
				return;
			}

			auto thread_it = _thread_heads.find(thread);
			assert(thread_it != _thread_heads.end() && "Thread must exist");
			auto& thread_event = thread_it->second;
			assert(thread_event->kind() != por::event::event_kind::thread_exit && "Thread must not yet be exited");
			assert(thread_event->kind() != por::event::event_kind::wait1 && "Thread must not be blocked");

			assert(lock > 0);
			assert(_lock_heads.find(lock) == _lock_heads.end() && "Lock id already taken");

			thread_event = event::lock_create::alloc(thread, std::move(thread_event));
			_lock_heads.emplace(lock, thread_event);

			_schedule.emplace_back(thread_event);
			++_schedule_pos;
			assert(_schedule_pos == _schedule.size());
		}

		void destroy_lock(event::thread_id_t thread, event::lock_id_t lock) {
			if(needs_catch_up()) {
				assert(_schedule[_schedule_pos]->kind() == por::event::event_kind::lock_destroy);
				assert(_schedule[_schedule_pos]->tid() == thread);
				_thread_heads[thread] = _schedule[_schedule_pos];
				_lock_heads.erase(lock);
				++_schedule_pos;
				return;
			}

			auto thread_it = _thread_heads.find(thread);
			assert(thread_it != _thread_heads.end() && "Thread must exist");
			auto& thread_event = thread_it->second;
			assert(thread_event->kind() != por::event::event_kind::thread_exit && "Thread must not yet be exited");
			assert(thread_event->kind() != por::event::event_kind::wait1 && "Thread must not be blocked");
			auto lock_it = _lock_heads.find(lock);
			if constexpr(optional_creation_events) {
				if(_lock_heads.find(lock) == _lock_heads.end()) {
					thread_event = event::lock_destroy::alloc(thread, std::move(thread_event), nullptr);

					_schedule.emplace_back(thread_event);
					++_schedule_pos;
					assert(_schedule_pos == _schedule.size());
					return;
				}
			}
			assert(lock_it != _lock_heads.end() && "Lock must (still) exist");
			auto& lock_event = lock_it->second;
			thread_event = event::lock_destroy::alloc(thread, std::move(thread_event), std::move(lock_event));
			_lock_heads.erase(lock_it);

			_schedule.emplace_back(thread_event);
			++_schedule_pos;
			assert(_schedule_pos == _schedule.size());
		}

		void acquire_lock(event::thread_id_t thread, event::lock_id_t lock) {
			if(needs_catch_up()) {
				assert(_schedule[_schedule_pos]->kind() == por::event::event_kind::lock_acquire);
				assert(_schedule[_schedule_pos]->tid() == thread);
				_thread_heads[thread] = _schedule[_schedule_pos];
				_lock_heads[lock] = _schedule[_schedule_pos];
				++_schedule_pos;
				return;
			}

			auto thread_it = _thread_heads.find(thread);
			assert(thread_it != _thread_heads.end() && "Thread must exist");
			auto& thread_event = thread_it->second;
			assert(thread_event->kind() != por::event::event_kind::thread_exit && "Thread must not yet be exited");
			assert(thread_event->kind() != por::event::event_kind::wait1 && "Thread must not be blocked");
			auto lock_it = _lock_heads.find(lock);
			if constexpr(optional_creation_events) {
				if(lock_it == _lock_heads.end()) {
					thread_event = event::lock_acquire::alloc(thread, std::move(thread_event), nullptr);
					_lock_heads.emplace(lock, thread_event);

					_schedule.emplace_back(thread_event);
					++_schedule_pos;
					assert(_schedule_pos == _schedule.size());
					return;
				}
			}
			assert(lock_it != _lock_heads.end() && "Lock must (still) exist");
			auto& lock_event = lock_it->second;
			thread_event = event::lock_acquire::alloc(thread, std::move(thread_event), std::move(lock_event));
			lock_event = thread_event;

			_schedule.emplace_back(thread_event);
			++_schedule_pos;
			assert(_schedule_pos == _schedule.size());
		}

		void release_lock(event::thread_id_t thread, event::lock_id_t lock) {
			if(needs_catch_up()) {
				assert(_schedule[_schedule_pos]->kind() == por::event::event_kind::lock_release);
				assert(_schedule[_schedule_pos]->tid() == thread);
				_thread_heads[thread] = _schedule[_schedule_pos];
				_lock_heads[lock] = _schedule[_schedule_pos];
				++_schedule_pos;
				return;
			}

			auto thread_it = _thread_heads.find(thread);
			assert(thread_it != _thread_heads.end() && "Thread must exist");
			auto& thread_event = thread_it->second;
			assert(thread_event->kind() != por::event::event_kind::thread_exit && "Thread must not yet be exited");
			assert(thread_event->kind() != por::event::event_kind::wait1 && "Thread must not be blocked");
			auto lock_it = _lock_heads.find(lock);
			if constexpr(optional_creation_events) {
				if(_lock_heads.find(lock) == _lock_heads.end()) {
					thread_event = event::lock_release::alloc(thread, std::move(thread_event), nullptr);
					_lock_heads.emplace(lock, thread_event);

					_schedule.emplace_back(thread_event);
					++_schedule_pos;
					assert(_schedule_pos == _schedule.size());
					return;
				}
			}
			assert(lock_it != _lock_heads.end() && "Lock must (still) exist");
			auto& lock_event = lock_it->second;
			thread_event = event::lock_release::alloc(thread, std::move(thread_event), std::move(lock_event));
			lock_event = thread_event;

			_schedule.emplace_back(thread_event);
			++_schedule_pos;
			assert(_schedule_pos == _schedule.size());
		}

		void create_cond(por::event::thread_id_t thread, por::event::cond_id_t cond) {
			if(needs_catch_up()) {
				assert(_schedule[_schedule_pos]->kind() == por::event::event_kind::condition_variable_create);
				assert(_schedule[_schedule_pos]->tid() == thread);
				_thread_heads[thread] = _schedule[_schedule_pos];
				_cond_heads.emplace(cond, std::vector{_schedule[_schedule_pos]});
				++_schedule_pos;
				return;
			}

			auto thread_it = _thread_heads.find(thread);
			assert(thread_it != _thread_heads.end() && "Thread must exist");
			auto& thread_event = thread_it->second;
			assert(thread_event->kind() != por::event::event_kind::thread_exit && "Thread must not yet be exited");
			assert(thread_event->kind() != por::event::event_kind::wait1 && "Thread must not be blocked");
			assert(cond > 0);
			assert(_cond_heads.find(cond) == _cond_heads.end() && "Condition variable id already taken");

			thread_event = por::event::condition_variable_create::alloc(thread, std::move(thread_event));
			_cond_heads.emplace(cond, std::vector{thread_event});

			_schedule.emplace_back(thread_event);
			++_schedule_pos;
			assert(_schedule_pos == _schedule.size());
		}

		void destroy_cond(por::event::thread_id_t thread, por::event::cond_id_t cond) {
			if(needs_catch_up()) {
				assert(_schedule[_schedule_pos]->kind() == por::event::event_kind::condition_variable_destroy);
				assert(_schedule[_schedule_pos]->tid() == thread);
				_thread_heads[thread] = _schedule[_schedule_pos];
				_cond_heads.erase(cond);
				++_schedule_pos;
				return;
			}

			auto thread_it = _thread_heads.find(thread);
			assert(thread_it != _thread_heads.end() && "Thread must exist");
			auto& thread_event = thread_it->second;
			assert(thread_event->kind() != por::event::event_kind::thread_exit && "Thread must not yet be exited");
			assert(thread_event->kind() != por::event::event_kind::wait1 && "Thread must not be blocked");
			auto cond_head_it = _cond_heads.find(cond);
			if constexpr(optional_creation_events) {
				if(cond_head_it == _cond_heads.end()) {
					thread_event = por::event::condition_variable_destroy::alloc(thread, std::move(thread_event), nullptr, nullptr);

					_schedule.emplace_back(thread_event);
					++_schedule_pos;
					assert(_schedule_pos == _schedule.size());
					return;
				}
			}
			assert(cond_head_it != _cond_heads.end() && "Condition variable must (still) exist");
			auto& cond_preds = cond_head_it->second;
			assert(cond_preds.size() > 0);

			thread_event = por::event::condition_variable_destroy::alloc(thread, std::move(thread_event), cond_preds.data(), cond_preds.data() + cond_preds.size());
			_cond_heads.erase(cond_head_it);

			_schedule.emplace_back(thread_event);
			++_schedule_pos;
			assert(_schedule_pos == _schedule.size());
		}

	private:
		std::vector<std::shared_ptr<por::event::event>> wait1_predecessors_cond(
			std::shared_ptr<por::event::event> const& thread_event,
			std::vector<std::shared_ptr<por::event::event>> const& cond_preds
		) {
			por::event::thread_id_t thread = thread_event->tid();
			std::vector<std::shared_ptr<por::event::event>> non_waiting;
			for(auto it = cond_preds.begin(); it != cond_preds.end(); ++it) {
				if((*it)->kind() == por::event::event_kind::wait1)
					continue;

				if((*it)->tid() == thread)
					continue; // excluded event is part of [thread_event]

				if((*it)->kind() == por::event::event_kind::signal) {
					auto sig = static_cast<por::event::signal const*>(it->get());
					if(!sig->is_lost())
						continue;
				}

				if((*it)->kind() == por::event::event_kind::broadcast) {
					auto bro = static_cast<por::event::broadcast const*>(it->get());
					if(bro->is_notifying_thread(thread))
						continue;
				}

				non_waiting.push_back(*it);
			}
			return non_waiting;
		}

	public:
		void wait1(por::event::thread_id_t thread, por::event::cond_id_t cond, por::event::lock_id_t lock) {
			if(needs_catch_up()) {
				assert(_schedule[_schedule_pos]->kind() == por::event::event_kind::wait1);
				assert(_schedule[_schedule_pos]->tid() == thread);
				_thread_heads[thread] = _schedule[_schedule_pos];
				_lock_heads[lock] = _schedule[_schedule_pos];
				_cond_heads[cond].push_back(_schedule[_schedule_pos]);
				++_schedule_pos;
				return;
			}

			auto thread_it = _thread_heads.find(thread);
			assert(thread_it != _thread_heads.end() && "Thread must exist");
			auto& thread_event = thread_it->second;
			assert(thread_event->kind() != por::event::event_kind::thread_exit && "Thread must not yet be exited");
			assert(thread_event->kind() != por::event::event_kind::wait1 && "Thread must not be blocked");
			auto cond_head_it = _cond_heads.find(cond);
			auto lock_it = _lock_heads.find(lock);
			if constexpr(optional_creation_events) {
				if(cond_head_it == _cond_heads.end()) {
					assert(lock_it != _lock_heads.end() && "Lock must (still) exist");
					auto& lock_event = lock_it->second;
					thread_event = por::event::wait1::alloc(thread, std::move(thread_event), std::move(lock_event), nullptr, nullptr);
					lock_event = thread_event;
					_cond_heads.emplace(cond, std::vector{thread_event});

					_schedule.emplace_back(thread_event);
					++_schedule_pos;
					assert(_schedule_pos == _schedule.size());
					return;
				}
			}
			assert(cond_head_it != _cond_heads.end() && "Condition variable must (still) exist");
			auto& cond_preds = cond_head_it->second;
			assert(lock_it != _lock_heads.end() && "Lock must (still) exist");
			auto& lock_event = lock_it->second;

			std::vector<std::shared_ptr<por::event::event>> non_waiting = wait1_predecessors_cond(thread_event, cond_preds);
			thread_event = por::event::wait1::alloc(thread, std::move(thread_event), std::move(lock_event), non_waiting.data(), non_waiting.data() + non_waiting.size());
			lock_event = thread_event;
			cond_preds.push_back(thread_event);

			_schedule.emplace_back(thread_event);
			++_schedule_pos;
			assert(_schedule_pos == _schedule.size());
		}

	private:
		std::shared_ptr<por::event::event> const& wait2_predecessor_cond(
			std::shared_ptr<por::event::event> const& thread_event,
			std::vector<std::shared_ptr<por::event::event>> const& cond_preds
		) {
			for(auto& e : cond_preds) {
				if(e->kind() == por::event::event_kind::broadcast) {
					auto bro = static_cast<por::event::broadcast const*>(e.get());
					if(bro->is_notifying_thread(thread_event->tid()))
						return e;
				} else if(e->kind() == por::event::event_kind::signal) {
					auto sig = static_cast<por::event::signal const*>(e.get());
					if(sig->notified_thread() == thread_event->tid())
						return e;
				}
			}
			assert(0 && "There has to be a notifying event before a wait2");
		}

	public:
		void wait2(por::event::thread_id_t thread, por::event::cond_id_t cond, por::event::lock_id_t lock) {
			if(needs_catch_up()) {
				assert(_schedule[_schedule_pos]->kind() == por::event::event_kind::wait2);
				assert(_schedule[_schedule_pos]->tid() == thread);
				_thread_heads[thread] = _schedule[_schedule_pos];
				_lock_heads[lock] = _schedule[_schedule_pos];
				++_schedule_pos;
				return;
			}

			auto thread_it = _thread_heads.find(thread);
			assert(thread_it != _thread_heads.end() && "Thread must exist");
			auto& thread_event = thread_it->second;
			assert(thread_event->kind() != por::event::event_kind::thread_exit && "Thread must not yet be exited");
			assert(thread_event->kind() == por::event::event_kind::wait1 && "Thread must be waiting");
			auto cond_head_it = _cond_heads.find(cond);
			assert(cond_head_it != _cond_heads.end() && "Condition variable must (still) exist");
			auto& cond_preds = cond_head_it->second;
			auto lock_it = _lock_heads.find(lock);
			assert(lock_it != _lock_heads.end() && "Lock must (still) exist");
			auto& lock_event = lock_it->second;

			auto& cond_event = wait2_predecessor_cond(thread_event, cond_preds);
			thread_event = por::event::wait2::alloc(thread, std::move(thread_event), std::move(lock_event), cond_event);
			lock_event = thread_event;

			_schedule.emplace_back(thread_event);
			++_schedule_pos;
			assert(_schedule_pos == _schedule.size());
		}

	private:
		auto notified_wait1_predecessor(
			por::event::thread_id_t notified_thread,
			std::vector<std::shared_ptr<por::event::event>>& cond_preds
		) {
			auto cond_it = std::find_if(cond_preds.begin(), cond_preds.end(), [&notified_thread](auto& e) {
				return e->tid() == notified_thread && e->kind() == por::event::event_kind::wait1;
			});
			assert(cond_it != cond_preds.end() && "Wait1 event must be in cond_heads");
			return cond_it;
		}

		// extracts non-lost notification events not included in [thread_event]
		// where thread_event is the same-thread predecessor of a signal or broadcast to be created
		std::vector<std::shared_ptr<por::event::event>> lost_notification_predecessors_cond(
			std::shared_ptr<por::event::event> const& thread_event,
			std::vector<std::shared_ptr<por::event::event>> const& cond_preds
		) {
			por::event::thread_id_t thread = thread_event->tid();
			std::vector<std::shared_ptr<por::event::event>> prev_notifications;
			for(auto it = cond_preds.begin(); it != cond_preds.end(); ++it) {
				if((*it)->tid() == thread)
					continue; // excluded event must be in [thread_event]

				if((*it)->kind() == por::event::event_kind::wait1) {
					assert(0 && "signal or broadcast would not have been lost");
				} else if((*it)->kind() == por::event::event_kind::broadcast) {
					auto bro = static_cast<por::event::broadcast const*>(it->get());
					if(bro->is_lost())
						continue;

					if(bro->is_notifying_thread(thread))
						continue; // excluded event must be in [thread_event]
				} else if((*it)->kind() == por::event::event_kind::signal) {
					auto sig = static_cast<por::event::signal const*>(it->get());
					if(sig->is_lost())
						continue;

					if(sig->notified_thread() == thread)
						continue; // excluded event must be in [thread_event]
				}

				// exclude events that are in [thread_event], such as a non-lost broadcast
				// event already included in the causes of a previous lost notification
				if((*it)->is_less_than(*thread_event))
					continue;

				prev_notifications.push_back(*it);
			}
			return prev_notifications;
		}

	public:
		void signal_thread(por::event::thread_id_t thread, por::event::cond_id_t cond, por::event::thread_id_t notified_thread) {
			if(needs_catch_up()) {
				assert(_schedule[_schedule_pos]->kind() == por::event::event_kind::signal);
				assert(_schedule[_schedule_pos]->tid() == thread);
				_thread_heads[thread] = _schedule[_schedule_pos];
				if(notified_thread == 0) {
					_cond_heads[cond].push_back(_schedule[_schedule_pos]);
				} else {
					*notified_wait1_predecessor(notified_thread, _cond_heads[cond]) = _schedule[_schedule_pos];
				}
				++_schedule_pos;
				return;
			}

			auto thread_it = _thread_heads.find(thread);
			assert(thread_it != _thread_heads.end() && "Thread must exist");
			auto& thread_event = thread_it->second;
			assert(thread_event->kind() != por::event::event_kind::thread_exit && "Thread must not yet be exited");
			assert(thread_event->kind() != por::event::event_kind::wait1 && "Thread must not be blocked");
			auto cond_head_it = _cond_heads.find(cond);
			if constexpr(optional_creation_events) {
				if(cond_head_it == _cond_heads.end() && notified_thread == 0) {
					thread_event = por::event::signal::alloc(thread, std::move(thread_event), nullptr, nullptr);
					_cond_heads.emplace(cond, std::vector{thread_event});

					_schedule.emplace_back(thread_event);
					++_schedule_pos;
					assert(_schedule_pos == _schedule.size());
					return thread_event;
				}
			}
			assert(cond_head_it != _cond_heads.end() && "Condition variable must (still) exist");
			auto& cond_preds = cond_head_it->second;

			if(notified_thread == 0) { // lost signal
				std::vector<std::shared_ptr<por::event::event>> prev_notifications = lost_notification_predecessors_cond(thread_event, cond_preds);
				thread_event = por::event::signal::alloc(thread, std::move(thread_event), prev_notifications.data(), prev_notifications.data() + prev_notifications.size());
				cond_preds.push_back(thread_event);
			} else { // notifying signal
				assert(notified_thread != thread && "Thread cannot notify itself");
				auto notified_thread_it = _thread_heads.find(notified_thread);
				assert(notified_thread_it != _thread_heads.end() && "Notified thread must exist");
				auto& notified_thread_event = notified_thread_it->second;
				assert(notified_thread_event->kind() != por::event::event_kind::thread_exit && "Thread must not yet be exited");
				assert(notified_thread_event->kind() == por::event::event_kind::wait1 && "Notified thread must be waiting");

				auto& cond_event = *notified_wait1_predecessor(notified_thread, cond_preds);
				assert(cond_event == notified_thread_event);

				thread_event = por::event::signal::alloc(thread, std::move(thread_event), std::move(cond_event));
				cond_event = thread_event;
			}

			_schedule.emplace_back(thread_event);
			++_schedule_pos;
			assert(_schedule_pos == _schedule.size());
		}

	public:
		void broadcast_threads(por::event::thread_id_t thread, por::event::cond_id_t cond, std::vector<por::event::thread_id_t> notified_threads) {
			if(needs_catch_up()) {
				assert(_schedule[_schedule_pos]->kind() == por::event::event_kind::broadcast);
				assert(_schedule[_schedule_pos]->tid() == thread);
				_thread_heads[thread] = _schedule[_schedule_pos];
				if(notified_threads.empty()) {
					_cond_heads[cond].push_back(_schedule[_schedule_pos]);
				} else {
					for(auto& nid : notified_threads) {
						_cond_heads[cond].erase(notified_wait1_predecessor(nid, _cond_heads[cond]));
					}
				}
				++_schedule_pos;
				return;
			}

			auto thread_it = _thread_heads.find(thread);
			assert(thread_it != _thread_heads.end() && "Thread must exist");
			auto& thread_event = thread_it->second;
			assert(thread_event->kind() != por::event::event_kind::thread_exit && "Thread must not yet be exited");
			assert(thread_event->kind() != por::event::event_kind::wait1 && "Thread must not be blocked");
			auto cond_head_it = _cond_heads.find(cond);
			if constexpr(optional_creation_events) {
				if(cond_head_it == _cond_heads.end() && notified_threads.empty()) {
					thread_event = por::event::broadcast::alloc(thread, std::move(thread_event), nullptr, nullptr);
					_cond_heads.emplace(cond, std::vector{thread_event});

					_schedule.emplace_back(thread_event);
					++_schedule_pos;
					assert(_schedule_pos == _schedule.size());
					return thread_event;
				}
			}
			assert(cond_head_it != _cond_heads.end() && "Condition variable must (still) exist");
			auto& cond_preds = cond_head_it->second;

			if(notified_threads.empty()) { // lost broadcast
				std::vector<std::shared_ptr<por::event::event>> prev_notifications = lost_notification_predecessors_cond(thread_event, cond_preds);
				thread_event = por::event::broadcast::alloc(thread, std::move(thread_event), prev_notifications.data(), prev_notifications.data() + prev_notifications.size());
				cond_preds.push_back(thread_event);
			} else { // notifying broadcast
				std::vector<std::shared_ptr<por::event::event>> prev_events;
				for(auto& nid : notified_threads) {
					assert(nid != thread && "Thread cannot notify itself");
					auto notified_thread_it = _thread_heads.find(nid);
					assert(notified_thread_it != _thread_heads.end() && "Notified thread must exist");
					auto& notified_thread_event = notified_thread_it->second;
					assert(notified_thread_event->kind() != por::event::event_kind::thread_exit && "Thread must not yet be exited");
					assert(notified_thread_event->kind() == por::event::event_kind::wait1 && "Notified thread must be waiting");

					auto cond_it = notified_wait1_predecessor(nid, cond_preds);
					auto& cond_event = *cond_it;
					assert(cond_event == notified_thread_event);

					prev_events.push_back(notified_thread_event);
					cond_preds.erase(cond_it);
				}

				for(auto it = cond_preds.begin(); it != cond_preds.end(); ++it) {
					if((*it)->kind() == por::event::event_kind::wait1 || (*it)->kind() == por::event::event_kind::condition_variable_create)
						continue;

					if((*it)->tid() == thread)
						continue; // excluded event is part of [thread_event]

					if((*it)->kind() == por::event::event_kind::broadcast)
						continue;

					if((*it)->kind() == por::event::event_kind::signal) {
						auto sig = static_cast<por::event::signal const*>(it->get());
						if(sig->is_lost())
							continue;

						if(std::find(notified_threads.begin(), notified_threads.end(), sig->notified_thread()) != notified_threads.end())
							continue;

						if(sig->notified_thread() == thread)
							continue;
					}

					prev_events.push_back(*it);
				}

				thread_event = por::event::broadcast::alloc(thread, std::move(thread_event), prev_events.data(), prev_events.data() + prev_events.size());
				cond_preds.push_back(thread_event);
			}

			_schedule.emplace_back(thread_event);
			++_schedule_pos;
			assert(_schedule_pos == _schedule.size());
		}

		void local(event::thread_id_t thread, std::vector<bool> path) {
			if(needs_catch_up()) {
				assert(_schedule[_schedule_pos]->kind() == por::event::event_kind::local);
				assert(_schedule[_schedule_pos]->tid() == thread);
				[[maybe_unused]] auto& cs = static_cast<por::event::local*>(_schedule[_schedule_pos].get())->path();
				assert(cs == path);
				_thread_heads[thread] = _schedule[_schedule_pos];
				++_schedule_pos;
				return;
			}
			auto thread_it = _thread_heads.find(thread);
			assert(thread_it != _thread_heads.end() && "Thread must exist");
			auto& thread_event = thread_it->second;
			assert(thread_event->kind() != por::event::event_kind::thread_exit && "Thread must not yet be exited");

			thread_event = event::local::alloc(thread, std::move(thread_event), std::move(path));

			_schedule.emplace_back(thread_event);
			++_schedule_pos;
			assert(_schedule_pos == _schedule.size());
		}

		std::shared_ptr<por::event::event> const* get_thread_predecessor(std::shared_ptr<por::event::event> const& event) {
			assert(event);
			std::shared_ptr<por::event::event> const* pred = nullptr;
			switch(event->kind()) {
				case por::event::event_kind::broadcast:
					pred = &static_cast<por::event::broadcast const*>(event.get())->thread_predecessor();
					break;
				case por::event::event_kind::condition_variable_create:
					pred = &static_cast<por::event::condition_variable_create const*>(event.get())->thread_predecessor();
					break;
				case por::event::event_kind::condition_variable_destroy:
					pred = &static_cast<por::event::condition_variable_destroy const*>(event.get())->thread_predecessor();
					break;
				case por::event::event_kind::local:
					pred = &static_cast<por::event::local const*>(event.get())->thread_predecessor();
					break;
				case por::event::event_kind::lock_acquire:
					pred = &static_cast<por::event::lock_acquire const*>(event.get())->thread_predecessor();
					break;
				case por::event::event_kind::lock_create:
					pred = &static_cast<por::event::lock_create const*>(event.get())->thread_predecessor();
					break;
				case por::event::event_kind::lock_destroy:
					pred = &static_cast<por::event::lock_destroy const*>(event.get())->thread_predecessor();
					break;
				case por::event::event_kind::lock_release:
					pred = &static_cast<por::event::lock_release const*>(event.get())->thread_predecessor();
					break;
				case por::event::event_kind::signal:
					pred = &static_cast<por::event::signal const*>(event.get())->thread_predecessor();
					break;
				case por::event::event_kind::thread_create:
					pred = &static_cast<por::event::thread_create const*>(event.get())->thread_predecessor();
					break;
				case por::event::event_kind::thread_exit:
					pred = &static_cast<por::event::thread_exit const*>(event.get())->thread_predecessor();
					break;
				case por::event::event_kind::thread_init:
					break;
				case por::event::event_kind::thread_join:
					pred = &static_cast<por::event::thread_join const*>(event.get())->thread_predecessor();
					break;
				case por::event::event_kind::wait1:
					pred = &static_cast<por::event::wait1 const*>(event.get())->thread_predecessor();
					break;
				case por::event::event_kind::wait2:
					pred = &static_cast<por::event::wait2 const*>(event.get())->thread_predecessor();
					break;

				default:
					assert(0 && "event has no thread_predecessor");
			}
			if(pred != nullptr && *pred != nullptr && (*pred)->kind() != por::event::event_kind::program_init) {
				return pred;
			}
			return nullptr;

		}

		std::shared_ptr<por::event::event> const* get_lock_predecessor(std::shared_ptr<por::event::event> const& event) {
			assert(event);
			std::shared_ptr<por::event::event> const* pred = nullptr;
			switch(event->kind()) {
				case por::event::event_kind::lock_acquire:
					pred = &static_cast<por::event::lock_acquire const*>(event.get())->lock_predecessor();
					break;
				case por::event::event_kind::lock_create:
					break;
				case por::event::event_kind::lock_destroy:
					pred = &static_cast<por::event::lock_destroy const*>(event.get())->lock_predecessor();
					break;
				case por::event::event_kind::lock_release:
					pred = &static_cast<por::event::lock_release const*>(event.get())->lock_predecessor();
					break;
				case por::event::event_kind::wait1:
					pred = &static_cast<por::event::wait1 const*>(event.get())->lock_predecessor();
					break;
				case por::event::event_kind::wait2:
					pred = &static_cast<por::event::wait2 const*>(event.get())->lock_predecessor();
					break;

				default:
					assert(0 && "event has no lock_predecessor");
			}
			if(pred != nullptr && *pred != nullptr && (*pred)->kind() != por::event::event_kind::lock_create) {
				return pred;
			}
			return nullptr;
		}

	private:
		template<typename UnaryPredicate>
		std::vector<std::vector<std::shared_ptr<por::event::event> const*>> concurrent_combinations(
			std::map<por::event::thread_id_t, std::vector<std::shared_ptr<por::event::event> const*>> &comb,
			UnaryPredicate filter
		) {
			std::vector<std::vector<std::shared_ptr<por::event::event> const*>> result;
			// compute all combinations: S \subseteq comb (where S is concurrent,
			// i.e. there are no causal dependencies between any of its elements)
			assert(comb.size() < 64);
			for(std::uint64_t mask = 0; mask < (1 << comb.size()); ++mask) {
				std::size_t popcount = 0;
				for(std::size_t i = 0; i < comb.size(); ++i) {
					if((mask >> i) & 1)
						++popcount;
				}
				if (popcount > 0) {
					// indexes of the threads enabled in current mask
					// (of which there are popcount-many)
					std::vector<por::event::thread_id_t> selected_threads;
					selected_threads.reserve(popcount);

					// maps a selected thread to the highest index present in its event vector
					// i.e. highest_index[i] == comb[selected_threads[i]].size() - 1
					std::vector<std::size_t> highest_index;
					highest_index.reserve(popcount);

					auto it = comb.begin();
					for(std::size_t i = 0; i < comb.size(); ++i, ++it) {
						assert(std::next(comb.begin(), i) == it);
						assert(std::next(comb.begin(), i) != comb.end());
						if((mask >> i) & 1) {
							selected_threads.push_back(it->first);
							highest_index.push_back(it->second.size() - 1);
						}
					}

					// index in the event vector of corresponding thread for
					// each selected thread, starting with all zeros
					std::vector<std::size_t> event_indices(popcount, 0);

					std::size_t pos = 0;
					while(pos < popcount) {
						// complete subset
						std::vector<std::shared_ptr<por::event::event> const*> subset;
						subset.reserve(popcount);
						bool is_concurrent = true;
						for(std::size_t k = 0; k < popcount; ++k) {
							auto& new_event = comb[selected_threads[k]][event_indices[k]];
							if(k > 0) {
								// check if new event is concurrent to previous ones
								for(auto& e : subset) {
									if((**e).is_less_than(**new_event) || (**new_event).is_less_than(**e)) {
										is_concurrent = false;
										break;
									}
								}
							}
							if(!is_concurrent)
								break;
							subset.push_back(new_event);
						}
						if(is_concurrent && filter(subset)) {
							result.push_back(std::move(subset));
						}

						// search for lowest position that can be incremented
						while(pos < popcount && event_indices[pos] == highest_index[pos]) {
							++pos;
						}

						if(pos == popcount && event_indices[pos - 1] == highest_index[pos - 1])
							break;

						++event_indices[pos];

						// reset lower positions and go back to pos = 0
						while(pos > 0) {
							--pos;
							event_indices[pos] = 0;
						}
					}
				} else {
					// empty set
					std::vector<std::shared_ptr<por::event::event> const*> empty;
					if(filter(empty)) {
						result.push_back(std::move(empty));
					}
				}
			}
			return result;
		}

		std::vector<conflicting_extension> cex_acquire(std::shared_ptr<por::event::event> const& e) {
			assert(e->kind() == por::event::event_kind::lock_acquire || e->kind() == por::event::event_kind::wait2);

			std::vector<conflicting_extension> result;

			// immediate causal predecessor on same thread
			std::shared_ptr<por::event::event> const* et = get_thread_predecessor(e);
			// maximal event concerning same lock in history of e
			std::shared_ptr<por::event::event> const* er = get_lock_predecessor(e);
			// maximal event concerning same lock in [et]
			std::shared_ptr<por::event::event> const* em = er;
			// immediate successor of em / ep operating on same lock
			std::shared_ptr<por::event::event> const* conflict = &e;
			// signaling event (only for wait2)
			std::shared_ptr<por::event::event> const* es = nullptr;

			assert(et != nullptr);

			if(e->kind() == por::event::event_kind::lock_acquire) {
				while(em != nullptr && !((**em).is_less_than(**et))) {
					// descend chain of lock events until em is in [et]
					conflict = em;
					em = get_lock_predecessor(*em);
				}
			} else {
				assert(e->kind() == por::event::event_kind::wait2);
				auto* w2 = static_cast<por::event::wait2 const*>(e.get());
				es = &w2->condition_variable_predecessor();
				assert(es != nullptr && *es != nullptr);
				while(em != nullptr && !((**em).is_less_than(**et)) && !((**em).is_less_than(**es))) {
					// descend chain of lock events until em is in [et] \cup [es]
					conflict = em;
					em = get_lock_predecessor(*em);
				}
			}

			if(em == er) {
				return {};
			}

			if(em == nullptr) {
				// (kind(em) == lock_release || kind(em) == wait1) is included in while loop below (with correct lock predecessor)
				result.emplace_back(por::event::lock_acquire::alloc(e->tid(), *et, nullptr), *conflict);
			}

			assert(er != nullptr); // if er is nullptr, em == er, so we already returned
			std::shared_ptr<por::event::event> const* ep = get_lock_predecessor(*er);
			conflict = er;
			while(ep != nullptr && em != nullptr && ((**em).is_less_than(**ep) || em == ep)) {
				if((*ep)->kind() == por::event::event_kind::lock_release || (*ep)->kind() == por::event::event_kind::wait1) {
					if(e->kind() == por::event::event_kind::lock_acquire) {
						result.emplace_back(por::event::lock_acquire::alloc(e->tid(), *et, *ep), *conflict);
					} else {
						assert(e->kind() == por::event::event_kind::wait2);
						result.emplace_back(por::event::wait2::alloc(e->tid(), *et, *ep, *es), *conflict);
					}
				}
				conflict = ep;
				ep = get_lock_predecessor(*ep);
			}

			return result;
		}

		// returns index of first conflict, i.e. first index that deviates from given schedule
		std::size_t compute_new_schedule_from_old(conflicting_extension const& cex, std::vector<std::shared_ptr<por::event::event>>& schedule) {
			[[maybe_unused]] std::size_t original_size = schedule.size();
			auto is_in_conflict = [&](auto& e) {
				for(auto& conflict : cex.conflicts()) {
					assert(conflict);
					assert(e);
					if((*conflict).is_less_than(*e) || conflict == e)
						return true;
				}
				return false;
			};
			auto first_conflict = std::find_if(schedule.begin(), schedule.end(), is_in_conflict);
			schedule.erase(std::remove_if(first_conflict, schedule.end(), is_in_conflict), schedule.end());
			assert(schedule.size() <= (original_size - cex.num_of_conflicts()));
			schedule.emplace_back(cex.new_event());
			return std::distance(schedule.begin(), first_conflict);
		}

		std::pair<std::vector<std::shared_ptr<por::event::event>>, std::size_t> compute_new_schedule_from_current(conflicting_extension const& cex) {
			std::vector<std::shared_ptr<por::event::event>> result = _schedule;
			std::size_t prefix = compute_new_schedule_from_old(cex, result);
			return std::make_pair(result, prefix);
		}

	public:
		std::vector<conflicting_extension> conflicting_extensions() {
			std::vector<conflicting_extension> S;
			std::vector<bool> maximal_path;
			for(auto event : _schedule) {
				if(event->kind() == por::event::event_kind::local) {
					auto local = static_cast<const por::event::local*>(event.get());
					auto &local_path = local->path();
					maximal_path.insert(maximal_path.end(), local_path.begin(), local_path.end());
				}
			}
			std::size_t path_length = maximal_path.size();

			// mark new paths of previously visited events as visited
			// NOTE: has to be done first so all paths can be found in deduplication process
			for(auto it = _schedule.rbegin(), ie = _schedule.rend(); it != ie; ++it) {
				auto path = std::vector<bool>(maximal_path.begin(), std::next(maximal_path.begin(), path_length));
				assert(path.size() == path_length);
				std::shared_ptr<por::event::event> const& e = *it;

				// TODO: remove duplicate code (shared with unfolding.h)
				if(path.empty()) {
					// remove all restrictions
					e->visited_paths.clear();
				} else if(!e->visited || !e->visited_paths.empty()) {
					e->visited_paths.emplace_back(std::move(path));
				}
			}

			for(auto it = _schedule.rbegin(), ie = _schedule.rend(); it != ie; ++it) {
				auto path = std::vector<bool>(maximal_path.begin(), std::next(maximal_path.begin(), path_length));
				std::shared_ptr<por::event::event> const& e = *it;
				if(!_unfolding->is_visited(e.get(), path).first) {
					switch(e->kind()) {
						case por::event::event_kind::lock_acquire:
						case por::event::event_kind::wait2: {
							auto r = cex_acquire(e);

							if(r.empty())
								break;

							for(auto& cex : r) {
								// compute path for cex
								auto cex_schedule = compute_new_schedule_from_current(cex).first;
								std::vector<bool> cex_path;
								for(auto event : cex_schedule) {
									if(event->kind() == por::event::event_kind::local) {
										auto local = static_cast<const por::event::local*>(event.get());
										auto &local_path = local->path();
										cex_path.insert(cex_path.end(), local_path.begin(), local_path.end());
									}
								}

								auto deduplicated = _unfolding->is_visited(cex.new_event().get(), cex_path);
								if(deduplicated.first)
									continue;
								if(deduplicated.second == cex.new_event().get()) {
									S.emplace_back(std::move(cex));
								} else {
									assert(cex.num_of_conflicts() == 1);
									S.emplace_back(const_cast<por::event::event*>(deduplicated.second)->shared_from_this(), *cex.conflicts().begin());
								}
							}

							break;
						}
					}
					_unfolding->mark_as_visited(e.get(), std::move(path));
				}
				// remove choices made in local events from path for previous ones
				// (as we are iterating in reverse)
				if(e->kind() == por::event::event_kind::local) {
					auto local = static_cast<const por::event::local*>(e.get());
					std::size_t future_choices = local->path().size();
					assert(path_length >= future_choices);
					path_length -= future_choices;
				}
			}
			return S;
		}
	};

	inline configuration configuration_root::construct() { return configuration(std::move(*this)); }
}
