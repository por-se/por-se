#pragma once

#include "cone.h"
#include "event/event.h"
#include "unfolding.h"

#include <util/sso_array.h>

#include <cassert>
#include <map>
#include <set>
#include <vector>
#include <memory>

namespace por {
	class configuration;
}

namespace klee {
	class ExecutionState;
	por::configuration* configuration_from_execution_state(klee::ExecutionState const* s);
}

namespace por {
	class conflicting_extension {
		por::event::event const& _new_event;
		util::sso_array<por::event::event const*, 1> _conflicting_events;

	public:
		conflicting_extension(por::event::event const& new_event, por::event::event const& conflicting_event)
		: _new_event(new_event)
		, _conflicting_events{util::create_uninitialized, 1ul}
		{
			_conflicting_events[0] = &conflicting_event;
		}

		conflicting_extension(por::event::event const& new_event, std::vector<por::event::event const*> conflicting_events)
		: _new_event(new_event)
		, _conflicting_events{util::create_uninitialized, conflicting_events.size()}
		{
			std::size_t index = 0;
			for(auto iter = conflicting_events.begin(); iter != conflicting_events.end(); ++iter, ++index) {
				assert(*iter != nullptr && "no nullptr in conflicting events allowed");
				_conflicting_events[index] = *iter;
			}
		}

		por::event::event const& new_event() const {
			return _new_event;
		}

		util::iterator_range<por::event::event const* const*> conflicts() const {
			return util::make_iterator_range<por::event::event const* const*>(_conflicting_events.data(), _conflicting_events.data() + _conflicting_events.size());
		}

		std::size_t num_of_conflicts() const {
			return _conflicting_events.size();
		}
	};

	class configuration_root {
		friend class configuration;

		std::shared_ptr<por::unfolding> _unfolding = std::make_shared<por::unfolding>();

		por::event::event const& _program_init = _unfolding->root();

		std::map<por::event::thread_id_t, por::event::event const*> _thread_heads;

	public:
		configuration construct();

		configuration_root& add_thread() {
			event::thread_id_t tid = thread_id(thread_id(), _thread_heads.size() + 1);

			_thread_heads.emplace(tid, &event::thread_init::alloc(*_unfolding, tid, _program_init));
			_unfolding->mark_as_explored(*_thread_heads[tid]);
			_unfolding->stats_inc_event_created(por::event::event_kind::thread_init);

			return *this;
		}
	};

	class configuration {
		// creation events for locks and condition variables are optional
		static const bool optional_creation_events = true;

		// the unfolding this configuration is part of
		std::shared_ptr<por::unfolding> _unfolding;

		// contains most recent event of ALL threads that ever existed within this configuration
		std::map<por::event::thread_id_t, por::event::event const*> _thread_heads;

		// contains most recent event of ACTIVE locks
		std::map<event::lock_id_t, por::event::event const*> _lock_heads;

		// contains all previous sig, bro events of ACTIVE condition variables for each thread
		std::map<por::event::cond_id_t, std::vector<por::event::event const*>> _cond_heads;

		// contains all previously used condition variable ids
		std::set<por::event::cond_id_t> _used_cond_ids;

		// symbolic choices made as manifested in local events in order of their execution
		por::event::path_t _path;

		// sequence of events in order of their execution
		std::vector<por::event::event const*> _schedule;

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
			_unfolding->stats_inc_event_created(por::event::event_kind::program_init);
			_unfolding->stats_inc_unique_event(por::event::event_kind::program_init);
			_schedule.emplace_back(&root._program_init);
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

		auto const& unfolding() const noexcept { return _unfolding; }

		// `previous` is the maximal configuration that was used to generate the conflict
		configuration(configuration const& previous, conflicting_extension const& cex)
			: _unfolding(previous._unfolding)
			, _schedule(previous._schedule)
			, _standby_states(previous._standby_states)
		{
			std::size_t conflict_gap = _schedule.size();

			// create new schedule and compute common prefix,
			// set _schedule_pos to latest theoretically possible standby
			_schedule_pos = compute_new_schedule_from_old(cex, _schedule) - 1;
			assert(_schedule_pos < _schedule.size());
			assert(_schedule.size() >= 2 && "there have to be at least program_init and thread_init");

			conflict_gap -= _schedule_pos;
			assert(conflict_gap >= 1);
			_unfolding->stats_inc_sum_of_conflict_gaps(conflict_gap);

			std::size_t catchup_gap = _schedule_pos;

			// there may not be a standby state for every schedule position
			if(_schedule_pos >= _standby_states.size())
				_schedule_pos = _standby_states.size() - 1;

			// find latest available standby state
			while(_schedule_pos > 0 && _standby_states[_schedule_pos] == nullptr) {
				--_schedule_pos;
			}
			assert(_schedule_pos >= 1 && "thread_init of main thread must always have a standby state");
			assert(_standby_states[_schedule_pos] != nullptr);

			// remove standby states that are invalid in the new schedule
			_standby_states.erase(std::next(_standby_states.begin(), _schedule_pos + 1), _standby_states.end());
			assert(_standby_states.size() - 1 == _schedule_pos);
			assert(_standby_states.back() != nullptr);

			catchup_gap -= _schedule_pos;
			_unfolding->stats_inc_sum_of_catchup_gaps(catchup_gap);

			// no need to catch up to standby state
			++_schedule_pos;

			// reset heads to values at standby
			configuration* partial = configuration_from_execution_state(_standby_states.back());
			assert(partial != nullptr);
			_thread_heads = partial->_thread_heads;
			_lock_heads = partial->_lock_heads;
			_cond_heads = partial->_cond_heads;
			_used_cond_ids = partial->_used_cond_ids;
			_path = partial->_path;

			_unfolding->stats_inc_cex_inserted();

			auto new_path = _path;
			bool modified = false;
			for(std::size_t i = _schedule_pos; i < _schedule.size(); ++i) {
				auto const& new_event = _schedule[i];
				if(!modified && new_event->kind() != por::event::event_kind::local) {
					if(i != _schedule.size() - 1)
						continue;
				}

				// already mark events with potentially modified path as open so
				// that none of them are generated as cex before catch up is done
				if(!_unfolding->is_present(*new_event, new_path)) {
					_unfolding->mark_as_open(*new_event, new_path);
				}

				if(new_event->kind() == por::event::event_kind::local) {
					modified = true; // at least potentially
					auto local = static_cast<por::event::local const*>(new_event);
					auto& local_path = local->path();
					new_path.insert(new_path.end(), local_path.begin(), local_path.end());
				}
			}
		}

		auto const& schedule() const noexcept { return _schedule; }
		bool needs_catch_up() const noexcept { return _schedule_pos < _schedule.size(); }

		por::event::event const* peek() const noexcept {
			if(!needs_catch_up()) {
				return nullptr;
			}
			return _schedule[_schedule_pos];
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

		std::size_t distance_to_last_standby_state(klee::ExecutionState const* s) {
			assert(s != nullptr);
			assert(_schedule.size() > 0);
			assert(_schedule_pos > 0);
			std::size_t res = 0;
			for(auto it = _standby_states.rbegin(); it != _standby_states.rend(); ++it) {
				if(*it != nullptr) {
					res = _schedule_pos - std::distance(it, _standby_states.rend());
					break;
				} else {
					++res;
				}
			}
			return res;
		}

		std::size_t active_threads() const noexcept {
			if(_thread_heads.size() == 0)
				return 0;

			std::size_t res = 0;
			for(auto& e : _thread_heads) {
				assert(!!e.second);
				if(e.second->kind() != por::event::event_kind::thread_exit && e.second->kind() != por::event::event_kind::wait1)
					++res;
			}
			return res;
		}

		// Spawn a new thread from tid `source`.
		std::pair<por::event::event const*, por::event::event const*>
		spawn_thread(event::thread_id_t source, por::event::thread_id_t new_tid) {
			if(needs_catch_up()) {
				assert(_schedule[_schedule_pos]->kind() == por::event::event_kind::thread_create);
				assert(_schedule[_schedule_pos]->tid() == source);
				_thread_heads[source] = _schedule[_schedule_pos];
				++_schedule_pos;
				assert(_schedule[_schedule_pos]->kind() == por::event::event_kind::thread_init);
				assert(_schedule[_schedule_pos]->tid() == new_tid);
				_thread_heads[new_tid] = _schedule[_schedule_pos];
				++_schedule_pos;
				return std::make_pair(_thread_heads[source], _thread_heads[new_tid]);
			}

			auto source_it = _thread_heads.find(source);
			assert(source_it != _thread_heads.end() && "Source thread must exist");
			auto& source_event = source_it->second;
			assert(source_event->kind() != por::event::event_kind::thread_exit && "Source thread must not yet be exited");
			assert(source_event->kind() != por::event::event_kind::wait1 && "Source thread must not be blocked");

			source_event = &event::thread_create::alloc(*_unfolding, source, *source_event);
			_unfolding->stats_inc_event_created(por::event::event_kind::thread_create);
			_unfolding->mark_as_open(*source_event, _path);
			assert(new_tid);
			assert(thread_heads().find(new_tid) == thread_heads().end() && "Thread with same id already exists");
			_thread_heads.emplace(new_tid, &event::thread_init::alloc(*_unfolding, new_tid, *source_event));
			_unfolding->stats_inc_event_created(por::event::event_kind::thread_init);
			_unfolding->mark_as_open(*_thread_heads[new_tid], _path);

			_schedule.emplace_back(source_event);
			_schedule.emplace_back(_thread_heads[new_tid]);
			_schedule_pos += 2;
			assert(_schedule_pos == _schedule.size());

			return std::make_pair(_thread_heads[source], _thread_heads[new_tid]);
		}

		por::event::event const* join_thread(event::thread_id_t thread, event::thread_id_t joined) {
			if(needs_catch_up()) {
				assert(_schedule[_schedule_pos]->kind() == por::event::event_kind::thread_join);
				assert(_schedule[_schedule_pos]->tid() == thread);
				_thread_heads[thread] = _schedule[_schedule_pos];
				++_schedule_pos;
				return _thread_heads[thread];
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

			thread_event = &event::thread_join::alloc(*_unfolding, thread, *thread_event, *joined_event);
			_unfolding->stats_inc_event_created(por::event::event_kind::thread_join);
			_unfolding->mark_as_open(*thread_event, _path);

			_schedule.emplace_back(thread_event);
			++_schedule_pos;
			assert(_schedule_pos == _schedule.size());
			return thread_event;
		}

		por::event::event const* exit_thread(event::thread_id_t thread) {
			if(needs_catch_up()) {
				assert(_schedule[_schedule_pos]->kind() == por::event::event_kind::thread_exit);
				assert(_schedule[_schedule_pos]->tid() == thread);
				_thread_heads[thread] = _schedule[_schedule_pos];
				++_schedule_pos;
				return _thread_heads[thread];
			}

			auto thread_it = _thread_heads.find(thread);
			assert(thread_it != _thread_heads.end() && "Thread must exist");
			auto& thread_event = thread_it->second;
			assert(thread_event->kind() != por::event::event_kind::thread_exit && "Thread must not yet be exited");
			assert(thread_event->kind() != por::event::event_kind::wait1 && "Thread must not be blocked");

			assert(active_threads() > 0);
			thread_event = &event::thread_exit::alloc(*_unfolding, thread, *thread_event);
			_unfolding->stats_inc_event_created(por::event::event_kind::thread_exit);
			_unfolding->mark_as_open(*thread_event, _path);

			_schedule.emplace_back(thread_event);
			++_schedule_pos;
			assert(_schedule_pos == _schedule.size());
			return thread_event;
		}

		por::event::event const* create_lock(event::thread_id_t thread, event::lock_id_t lock) {
			if(needs_catch_up()) {
				assert(_schedule[_schedule_pos]->kind() == por::event::event_kind::lock_create);
				assert(_schedule[_schedule_pos]->tid() == thread);
				_thread_heads[thread] = _schedule[_schedule_pos];
				_lock_heads.emplace(lock, _schedule[_schedule_pos]);
				++_schedule_pos;
				return _thread_heads[thread];
			}

			auto thread_it = _thread_heads.find(thread);
			assert(thread_it != _thread_heads.end() && "Thread must exist");
			auto& thread_event = thread_it->second;
			assert(thread_event->kind() != por::event::event_kind::thread_exit && "Thread must not yet be exited");
			assert(thread_event->kind() != por::event::event_kind::wait1 && "Thread must not be blocked");

			assert(lock > 0);
			assert(_lock_heads.find(lock) == _lock_heads.end() && "Lock id already taken");

			thread_event = &event::lock_create::alloc(*_unfolding, thread, *thread_event);
			_unfolding->stats_inc_event_created(por::event::event_kind::lock_create);
			_unfolding->mark_as_open(*thread_event, _path);
			_lock_heads.emplace(lock, thread_event);

			_schedule.emplace_back(thread_event);
			++_schedule_pos;
			assert(_schedule_pos == _schedule.size());
			return thread_event;
		}

		por::event::event const* destroy_lock(event::thread_id_t thread, event::lock_id_t lock) {
			if(needs_catch_up()) {
				assert(_schedule[_schedule_pos]->kind() == por::event::event_kind::lock_destroy);
				assert(_schedule[_schedule_pos]->tid() == thread);
				_thread_heads[thread] = _schedule[_schedule_pos];
				_lock_heads.erase(lock);
				++_schedule_pos;
				return _thread_heads[thread];
			}

			auto thread_it = _thread_heads.find(thread);
			assert(thread_it != _thread_heads.end() && "Thread must exist");
			auto& thread_event = thread_it->second;
			assert(thread_event->kind() != por::event::event_kind::thread_exit && "Thread must not yet be exited");
			assert(thread_event->kind() != por::event::event_kind::wait1 && "Thread must not be blocked");
			auto lock_it = _lock_heads.find(lock);
			if constexpr(optional_creation_events) {
				if(_lock_heads.find(lock) == _lock_heads.end()) {
					thread_event = &event::lock_destroy::alloc(*_unfolding, thread, *thread_event, nullptr);
					_unfolding->stats_inc_event_created(por::event::event_kind::lock_destroy);
					_unfolding->mark_as_open(*thread_event, _path);

					_schedule.emplace_back(thread_event);
					++_schedule_pos;
					assert(_schedule_pos == _schedule.size());
					return thread_event;
				}
			}
			assert(lock_it != _lock_heads.end() && "Lock must (still) exist");
			auto& lock_event = lock_it->second;
			thread_event = &event::lock_destroy::alloc(*_unfolding, thread, *thread_event, lock_event);
			_unfolding->stats_inc_event_created(por::event::event_kind::lock_destroy);
			_unfolding->mark_as_open(*thread_event, _path);
			_lock_heads.erase(lock_it);

			_schedule.emplace_back(thread_event);
			++_schedule_pos;
			assert(_schedule_pos == _schedule.size());
			return thread_event;
		}

		por::event::event const* acquire_lock(event::thread_id_t thread, event::lock_id_t lock) {
			if(needs_catch_up()) {
				assert(_schedule[_schedule_pos]->kind() == por::event::event_kind::lock_acquire);
				assert(_schedule[_schedule_pos]->tid() == thread);
				_thread_heads[thread] = _schedule[_schedule_pos];
				_lock_heads[lock] = _schedule[_schedule_pos];
				++_schedule_pos;
				return _thread_heads[thread];
			}

			auto thread_it = _thread_heads.find(thread);
			assert(thread_it != _thread_heads.end() && "Thread must exist");
			auto& thread_event = thread_it->second;
			assert(thread_event->kind() != por::event::event_kind::thread_exit && "Thread must not yet be exited");
			assert(thread_event->kind() != por::event::event_kind::wait1 && "Thread must not be blocked");
			auto lock_it = _lock_heads.find(lock);
			if constexpr(optional_creation_events) {
				if(lock_it == _lock_heads.end()) {
					thread_event = &event::lock_acquire::alloc(*_unfolding, thread, *thread_event, nullptr);
					_unfolding->stats_inc_event_created(por::event::event_kind::lock_acquire);
					_unfolding->mark_as_open(*thread_event, _path);
					_lock_heads.emplace(lock, thread_event);

					_schedule.emplace_back(thread_event);
					++_schedule_pos;
					assert(_schedule_pos == _schedule.size());
					return thread_event;
				}
			}
			assert(lock_it != _lock_heads.end() && "Lock must (still) exist");
			auto& lock_event = lock_it->second;
			thread_event = &event::lock_acquire::alloc(*_unfolding, thread, *thread_event, lock_event);
			_unfolding->stats_inc_event_created(por::event::event_kind::lock_acquire);
			_unfolding->mark_as_open(*thread_event, _path);
			lock_event = thread_event;

			_schedule.emplace_back(thread_event);
			++_schedule_pos;
			assert(_schedule_pos == _schedule.size());
			return thread_event;
		}

		por::event::event const* release_lock(event::thread_id_t thread, event::lock_id_t lock) {
			if(needs_catch_up()) {
				assert(_schedule[_schedule_pos]->kind() == por::event::event_kind::lock_release);
				assert(_schedule[_schedule_pos]->tid() == thread);
				_thread_heads[thread] = _schedule[_schedule_pos];
				_lock_heads[lock] = _schedule[_schedule_pos];
				++_schedule_pos;
				return _thread_heads[thread];
			}

			auto thread_it = _thread_heads.find(thread);
			assert(thread_it != _thread_heads.end() && "Thread must exist");
			auto& thread_event = thread_it->second;
			assert(thread_event->kind() != por::event::event_kind::thread_exit && "Thread must not yet be exited");
			assert(thread_event->kind() != por::event::event_kind::wait1 && "Thread must not be blocked");
			auto lock_it = _lock_heads.find(lock);
			assert(lock_it != _lock_heads.end() && "Lock must (still) exist");
			auto& lock_event = lock_it->second;
			thread_event = &event::lock_release::alloc(*_unfolding, thread, *thread_event, *lock_event);
			_unfolding->stats_inc_event_created(por::event::event_kind::lock_release);
			_unfolding->mark_as_open(*thread_event, _path);
			lock_event = thread_event;

			_schedule.emplace_back(thread_event);
			++_schedule_pos;
			assert(_schedule_pos == _schedule.size());
			return thread_event;
		}

		por::event::event const* create_cond(por::event::thread_id_t thread, por::event::cond_id_t cond) {
			if(needs_catch_up()) {
				assert(_schedule[_schedule_pos]->kind() == por::event::event_kind::condition_variable_create);
				assert(_schedule[_schedule_pos]->tid() == thread);
				assert(static_cast<por::event::condition_variable_create const*>(_schedule[_schedule_pos])->cid() == cond);
				_thread_heads[thread] = _schedule[_schedule_pos];
				assert(_used_cond_ids.count(cond) == 0 && "Condition variable id cannot be reused");
				_cond_heads.emplace(cond, std::vector{_schedule[_schedule_pos]});
				++_schedule_pos;
				return _thread_heads[thread];
			}

			auto thread_it = _thread_heads.find(thread);
			assert(thread_it != _thread_heads.end() && "Thread must exist");
			auto& thread_event = thread_it->second;
			assert(thread_event->kind() != por::event::event_kind::thread_exit && "Thread must not yet be exited");
			assert(thread_event->kind() != por::event::event_kind::wait1 && "Thread must not be blocked");
			assert(cond > 0 && "Condition variable id must not be zero");
			assert(_cond_heads.find(cond) == _cond_heads.end() && "Condition variable id already taken");
			assert(_used_cond_ids.count(cond) == 0 && "Condition variable id cannot be reused");

			thread_event = &por::event::condition_variable_create::alloc(*_unfolding, thread, cond, *thread_event);
			_unfolding->stats_inc_event_created(por::event::event_kind::condition_variable_create);
			_unfolding->mark_as_open(*thread_event, _path);
			_cond_heads.emplace(cond, std::vector{thread_event});
			_used_cond_ids.insert(cond);

			_schedule.emplace_back(thread_event);
			++_schedule_pos;
			assert(_schedule_pos == _schedule.size());
			return thread_event;
		}

		por::event::event const* destroy_cond(por::event::thread_id_t thread, por::event::cond_id_t cond) {
			if(needs_catch_up()) {
				assert(_schedule[_schedule_pos]->kind() == por::event::event_kind::condition_variable_destroy);
				assert(_schedule[_schedule_pos]->tid() == thread);
				_thread_heads[thread] = _schedule[_schedule_pos];
				_cond_heads.erase(cond);
				++_schedule_pos;
				return _thread_heads[thread];
			}

			auto thread_it = _thread_heads.find(thread);
			assert(thread_it != _thread_heads.end() && "Thread must exist");
			auto& thread_event = thread_it->second;
			assert(thread_event->kind() != por::event::event_kind::thread_exit && "Thread must not yet be exited");
			assert(thread_event->kind() != por::event::event_kind::wait1 && "Thread must not be blocked");
			auto cond_head_it = _cond_heads.find(cond);
			if constexpr(optional_creation_events) {
				assert(cond > 0 && "Condition variable id must not be zero");
				if(cond_head_it == _cond_heads.end()) {
					thread_event = &por::event::condition_variable_destroy::alloc(*_unfolding, thread, cond, *thread_event, {});
					_unfolding->stats_inc_event_created(por::event::event_kind::condition_variable_destroy);
					_unfolding->mark_as_open(*thread_event, _path);
					_used_cond_ids.insert(cond);

					_schedule.emplace_back(thread_event);
					++_schedule_pos;
					assert(_schedule_pos == _schedule.size());
					return thread_event;
				}
			}
			assert(cond_head_it != _cond_heads.end() && "Condition variable must (still) exist");
			auto& cond_preds = cond_head_it->second;
			assert(cond_preds.size() > 0);

			thread_event = &por::event::condition_variable_destroy::alloc(*_unfolding, thread, cond, *thread_event, cond_preds);
			_unfolding->stats_inc_event_created(por::event::event_kind::condition_variable_destroy);
			_unfolding->mark_as_open(*thread_event, _path);
			_cond_heads.erase(cond_head_it);

			_schedule.emplace_back(thread_event);
			++_schedule_pos;
			assert(_schedule_pos == _schedule.size());
			return thread_event;
		}

	private:
		std::vector<por::event::event const*> wait1_predecessors_cond(
			por::event::event const& thread_event,
			std::vector<por::event::event const*> const& cond_preds
		) {
			por::event::thread_id_t thread = thread_event.tid();
			std::vector<por::event::event const*> non_waiting;
			for(auto it = cond_preds.begin(); it != cond_preds.end(); ++it) {
				if((*it)->kind() == por::event::event_kind::wait1)
					continue;

				if((*it)->kind() == por::event::event_kind::signal) {
					auto sig = static_cast<por::event::signal const*>(*it);
					if(!sig->is_lost())
						continue;
				}

				if((*it)->kind() == por::event::event_kind::broadcast) {
					auto bro = static_cast<por::event::broadcast const*>(*it);
					if(bro->is_notifying_thread(thread))
						continue;
				}

				if((*it)->tid() == thread_event.tid())
					continue; // excluded event is in [thread_event]

				if((*it)->is_less_than_eq(thread_event))
					continue; // excluded event is in [thread_event]

				non_waiting.push_back(*it);
			}
			return non_waiting;
		}

	public:
		por::event::event const*
		wait1(por::event::thread_id_t thread, por::event::cond_id_t cond, por::event::lock_id_t lock) {
			if(needs_catch_up()) {
				assert(_schedule[_schedule_pos]->kind() == por::event::event_kind::wait1);
				assert(_schedule[_schedule_pos]->tid() == thread);
				_thread_heads[thread] = _schedule[_schedule_pos];
				_lock_heads[lock] = _schedule[_schedule_pos];
				_cond_heads[cond].push_back(_schedule[_schedule_pos]);
				++_schedule_pos;
				return _thread_heads[thread];
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
					assert(cond > 0 && "Condition variable id must not be zero");
					assert(lock_it != _lock_heads.end() && "Lock must (still) exist");
					auto& lock_event = lock_it->second;
					thread_event = &por::event::wait1::alloc(*_unfolding, thread, cond, *thread_event, *lock_event, {});
					_unfolding->stats_inc_event_created(por::event::event_kind::wait1);
					_unfolding->mark_as_open(*thread_event, _path);
					lock_event = thread_event;
					_cond_heads.emplace(cond, std::vector{thread_event});
					_used_cond_ids.insert(cond);

					_schedule.emplace_back(thread_event);
					++_schedule_pos;
					assert(_schedule_pos == _schedule.size());
					return thread_event;
				}
			}

			assert(cond_head_it != _cond_heads.end() && "Condition variable must (still) exist");
			auto& cond_preds = cond_head_it->second;
			assert(lock_it != _lock_heads.end() && "Lock must (still) exist");
			auto& lock_event = lock_it->second;

			std::vector<por::event::event const*> non_waiting = wait1_predecessors_cond(*thread_event, cond_preds);
			thread_event = &por::event::wait1::alloc(*_unfolding, thread, cond, *thread_event, *lock_event, std::move(non_waiting));
			_unfolding->stats_inc_event_created(por::event::event_kind::wait1);
			_unfolding->mark_as_open(*thread_event, _path);
			lock_event = thread_event;
			cond_preds.push_back(thread_event);

			_schedule.emplace_back(thread_event);
			++_schedule_pos;
			assert(_schedule_pos == _schedule.size());
			return thread_event;
		}

	private:
		por::event::event const& wait2_predecessor_cond(
			por::event::event const& thread_event,
			std::vector<por::event::event const*> const& cond_preds
		) {
			for(auto& e : cond_preds) {
				if(e->kind() == por::event::event_kind::broadcast) {
					auto bro = static_cast<por::event::broadcast const*>(e);
					for(auto& w1 : bro->wait_predecessors()) {
						if(w1 == &thread_event)
							return *e;
					}
				} else if(e->kind() == por::event::event_kind::signal) {
					auto sig = static_cast<por::event::signal const*>(e);
					if(sig->wait_predecessor() == &thread_event)
						return *e;
				}
			}
			assert(0 && "There has to be a notifying event before a wait2");
			std::abort();
		}

	public:
		por::event::event const*
		wait2(por::event::thread_id_t thread, por::event::cond_id_t cond, por::event::lock_id_t lock) {
			if(needs_catch_up()) {
				assert(_schedule[_schedule_pos]->kind() == por::event::event_kind::wait2);
				assert(_schedule[_schedule_pos]->tid() == thread);
				_thread_heads[thread] = _schedule[_schedule_pos];
				_lock_heads[lock] = _schedule[_schedule_pos];
				++_schedule_pos;
				return _thread_heads[thread];
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

			auto& cond_event = wait2_predecessor_cond(*thread_event, cond_preds);
			thread_event = &por::event::wait2::alloc(*_unfolding, thread, cond, *thread_event, *lock_event, cond_event);
			_unfolding->stats_inc_event_created(por::event::event_kind::wait2);
			_unfolding->mark_as_open(*thread_event, _path);
			lock_event = thread_event;

			_schedule.emplace_back(thread_event);
			++_schedule_pos;
			assert(_schedule_pos == _schedule.size());
			return thread_event;
		}

	private:
		auto notified_wait1_predecessor(
			por::event::thread_id_t notified_thread,
			std::vector<por::event::event const*>& cond_preds
		) {
			auto cond_it = std::find_if(cond_preds.begin(), cond_preds.end(), [&notified_thread](auto& e) {
				return e->tid() == notified_thread && e->kind() == por::event::event_kind::wait1;
			});
			assert(cond_it != cond_preds.end() && "Wait1 event must be in cond_heads");
			return cond_it;
		}

		// extracts non-lost notification events not included in [thread_event]
		// where thread_event is the same-thread predecessor of a signal or broadcast to be created
		std::vector<por::event::event const*> lost_notification_predecessors_cond(
			por::event::event const& thread_event,
			std::vector<por::event::event const*> const& cond_preds
		) {
			std::vector<por::event::event const*> prev_notifications;
			for(auto it = cond_preds.begin(); it != cond_preds.end(); ++it) {
				if((*it)->kind() == por::event::event_kind::wait1) {
					assert(0 && "signal or broadcast would not have been lost");
				} else if((*it)->kind() == por::event::event_kind::broadcast) {
					auto bro = static_cast<por::event::broadcast const*>(*it);
					if(bro->is_lost())
						continue;

					if(bro->is_notifying_thread(thread_event.tid()))
						continue; // excluded event is in [thread_event]
				} else if((*it)->kind() == por::event::event_kind::signal) {
					auto sig = static_cast<por::event::signal const*>(*it);
					if(sig->is_lost())
						continue;

					if(sig->notified_thread() == thread_event.tid())
						continue; // excluded event is in [thread_event]
				}

				if((*it)->tid() == thread_event.tid())
					continue; // excluded event is in [thread_event]

				if((*it)->is_less_than_eq(thread_event))
					continue; // excluded event is in [thread_event]

				prev_notifications.push_back(*it);
			}
			return prev_notifications;
		}

	public:
		por::event::event const*
		signal_thread(por::event::thread_id_t thread, por::event::cond_id_t cond, por::event::thread_id_t notified_thread) {
			if(needs_catch_up()) {
				assert(_schedule[_schedule_pos]->kind() == por::event::event_kind::signal);
				assert(_schedule[_schedule_pos]->tid() == thread);
				_thread_heads[thread] = _schedule[_schedule_pos];
				if(!notified_thread) { // lost signal
					_cond_heads[cond].push_back(_schedule[_schedule_pos]);
				} else {
					*notified_wait1_predecessor(notified_thread, _cond_heads[cond]) = _schedule[_schedule_pos];
				}
				++_schedule_pos;
				return _thread_heads[thread];
			}

			auto thread_it = _thread_heads.find(thread);
			assert(thread_it != _thread_heads.end() && "Thread must exist");
			auto& thread_event = thread_it->second;
			assert(thread_event->kind() != por::event::event_kind::thread_exit && "Thread must not yet be exited");
			assert(thread_event->kind() != por::event::event_kind::wait1 && "Thread must not be blocked");
			auto cond_head_it = _cond_heads.find(cond);
			if constexpr(optional_creation_events) {
				if(cond_head_it == _cond_heads.end() && !notified_thread) {
					// only possible for lost signal: otherwise there would be at least a wait1 in _cond_heads
					assert(cond > 0 && "Condition variable id must not be zero");
					thread_event = &por::event::signal::alloc(*_unfolding, thread, cond, *thread_event, std::vector<por::event::event const*>());
					_unfolding->stats_inc_event_created(por::event::event_kind::signal);
					_unfolding->mark_as_open(*thread_event, _path);
					_cond_heads.emplace(cond, std::vector{thread_event});
					_used_cond_ids.insert(cond);

					_schedule.emplace_back(thread_event);
					++_schedule_pos;
					assert(_schedule_pos == _schedule.size());
					return thread_event;
				}
			}
			assert(cond_head_it != _cond_heads.end() && "Condition variable must (still) exist");
			auto& cond_preds = cond_head_it->second;

			if(!notified_thread) { // lost signal
				auto prev_notifications = lost_notification_predecessors_cond(*thread_event, cond_preds);
				thread_event = &por::event::signal::alloc(*_unfolding, thread, cond, *thread_event, std::move(prev_notifications));
				_unfolding->stats_inc_event_created(por::event::event_kind::signal);
				_unfolding->mark_as_open(*thread_event, _path);
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

				thread_event = &por::event::signal::alloc(*_unfolding, thread, cond, *thread_event, *cond_event);
				_unfolding->stats_inc_event_created(por::event::event_kind::signal);
				_unfolding->mark_as_open(*thread_event, _path);
				cond_event = thread_event;
			}

			_schedule.emplace_back(thread_event);
			++_schedule_pos;
			assert(_schedule_pos == _schedule.size());
			return thread_event;
		}

	public:
		por::event::event const*
		broadcast_threads(por::event::thread_id_t thread, por::event::cond_id_t cond, std::vector<por::event::thread_id_t> notified_threads) {
			if(needs_catch_up()) {
				assert(_schedule[_schedule_pos]->kind() == por::event::event_kind::broadcast);
				assert(_schedule[_schedule_pos]->tid() == thread);
				_thread_heads[thread] = _schedule[_schedule_pos];
				if(!notified_threads.empty()) {
					for(auto& nid : notified_threads) {
						_cond_heads[cond].erase(notified_wait1_predecessor(nid, _cond_heads[cond]));
					}
				}
				_cond_heads[cond].push_back(_schedule[_schedule_pos]);
				++_schedule_pos;
				return _thread_heads[thread];
			}

			auto thread_it = _thread_heads.find(thread);
			assert(thread_it != _thread_heads.end() && "Thread must exist");
			auto& thread_event = thread_it->second;
			assert(thread_event->kind() != por::event::event_kind::thread_exit && "Thread must not yet be exited");
			assert(thread_event->kind() != por::event::event_kind::wait1 && "Thread must not be blocked");
			auto cond_head_it = _cond_heads.find(cond);
			if constexpr(optional_creation_events) {
				if(cond_head_it == _cond_heads.end() && notified_threads.empty()) {
					// only possible for lost broadcast: otherwise there would be at least a wait1 in _cond_heads
					assert(cond > 0 && "Condition variable id must not be zero");
					thread_event = &por::event::broadcast::alloc(*_unfolding, thread, cond, *thread_event, {});
					_unfolding->stats_inc_event_created(por::event::event_kind::broadcast);
					_unfolding->mark_as_open(*thread_event, _path);
					_cond_heads.emplace(cond, std::vector{thread_event});
					_used_cond_ids.insert(cond);

					_schedule.emplace_back(thread_event);
					++_schedule_pos;
					assert(_schedule_pos == _schedule.size());
					return thread_event;
				}
			}
			assert(cond_head_it != _cond_heads.end() && "Condition variable must (still) exist");
			auto& cond_preds = cond_head_it->second;

			if(notified_threads.empty()) { // lost broadcast
				auto prev_notifications = lost_notification_predecessors_cond(*thread_event, cond_preds);
				thread_event = &por::event::broadcast::alloc(*_unfolding, thread, cond, *thread_event, std::move(prev_notifications));
				_unfolding->stats_inc_event_created(por::event::event_kind::broadcast);
				_unfolding->mark_as_open(*thread_event, _path);
				cond_preds.push_back(thread_event);
			} else { // notifying broadcast
				std::vector<por::event::event const*> prev_events;
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
					// remove notified wait1 events
					cond_preds.erase(cond_it);
				}

				for(auto it = cond_preds.begin(); it != cond_preds.end(); ++it) {
					if((*it)->kind() == por::event::event_kind::wait1)
						continue; // relevant wait1s already part of prev_events

					if((*it)->kind() == por::event::event_kind::condition_variable_create)
						continue; // excluded event is included in wait1's causes (if it exists)

					if((*it)->kind() == por::event::event_kind::broadcast)
						continue;

					if((*it)->kind() == por::event::event_kind::signal) {
						auto sig = static_cast<por::event::signal const*>(*it);
						if(sig->is_lost())
							continue;

						if(sig->notified_thread() == thread)
							continue; // excluded event is in [thread_event]

						if(std::find(notified_threads.begin(), notified_threads.end(), sig->notified_thread()) != notified_threads.end())
							continue;
					}

					if((*it)->tid() == thread)
						continue; // excluded event is in [thread_event]

					if((*it)->is_less_than_eq(*thread_event))
						continue; // excluded event is in [thread_event]

					prev_events.push_back(*it);
				}

				thread_event = &por::event::broadcast::alloc(*_unfolding, thread, cond, *thread_event, std::move(prev_events));
				_unfolding->stats_inc_event_created(por::event::event_kind::broadcast);
				_unfolding->mark_as_open(*thread_event, _path);
				cond_preds.push_back(thread_event);
			}

			_schedule.emplace_back(thread_event);
			++_schedule_pos;
			assert(_schedule_pos == _schedule.size());
			return thread_event;
		}

		por::event::event const* local(event::thread_id_t thread, event::path_t local_path) {
			if(needs_catch_up()) {
				assert(_schedule[_schedule_pos]->kind() == por::event::event_kind::local);
				assert(_schedule[_schedule_pos]->tid() == thread);
				[[maybe_unused]] auto& cs = static_cast<por::event::local const*>(_schedule[_schedule_pos])->path();
				assert(cs == local_path);
				_thread_heads[thread] = _schedule[_schedule_pos];
				_path.insert(_path.end(), local_path.begin(), local_path.end());
				++_schedule_pos;
				return _thread_heads[thread];
			}
			auto thread_it = _thread_heads.find(thread);
			assert(thread_it != _thread_heads.end() && "Thread must exist");
			auto& thread_event = thread_it->second;
			assert(thread_event->kind() != por::event::event_kind::thread_exit && "Thread must not yet be exited");

			auto old_path = _path;
			_path.insert(_path.end(), local_path.begin(), local_path.end());
			thread_event = &event::local::alloc(*_unfolding, thread, *thread_event, std::move(local_path));
			_unfolding->stats_inc_event_created(por::event::event_kind::local);
			_unfolding->mark_as_open(*thread_event, std::move(old_path));

			_schedule.emplace_back(thread_event);
			++_schedule_pos;
			assert(_schedule_pos == _schedule.size());
			return thread_event;
		}

		static util::iterator_range<por::event::event const* const*> get_condition_variable_predecessors(por::event::event const* event) {
			assert(event);
			auto preds = util::make_iterator_range<por::event::event const* const*>(nullptr, nullptr);
			switch(event->kind()) {
				case por::event::event_kind::broadcast:
					preds = static_cast<por::event::broadcast const*>(event)->condition_variable_predecessors();
					break;
				case por::event::event_kind::condition_variable_create:
					break;
				case por::event::event_kind::condition_variable_destroy:
					preds = static_cast<por::event::condition_variable_destroy const*>(event)->condition_variable_predecessors();
					break;
				case por::event::event_kind::signal:
					preds = static_cast<por::event::signal const*>(event)->condition_variable_predecessors();
					break;
				case por::event::event_kind::wait1:
					preds = static_cast<por::event::wait1 const*>(event)->condition_variable_predecessors();
					break;
				case por::event::event_kind::wait2:
					preds = static_cast<por::event::wait2 const*>(event)->condition_variable_predecessors();
					break;
				default:
					assert(0 && "event has no condition_variable_predecessors");
			}
			return preds;
		}

	private:
		static por::event::cond_id_t get_cid(por::event::event const* event) {
			assert(event != nullptr);
			por::event::cond_id_t result = 0;
			switch(event->kind()) {
				case por::event::event_kind::broadcast:
					result = static_cast<por::event::broadcast const*>(event)->cid();
					break;
				case por::event::event_kind::condition_variable_create:
					result = static_cast<por::event::condition_variable_create const*>(event)->cid();
					break;
				case por::event::event_kind::condition_variable_destroy:
					result = static_cast<por::event::condition_variable_destroy const*>(event)->cid();
					break;
				case por::event::event_kind::signal:
					result = static_cast<por::event::signal const*>(event)->cid();
					break;
				case por::event::event_kind::wait1:
					result = static_cast<por::event::wait1 const*>(event)->cid();
					break;
				case por::event::event_kind::wait2:
					result = static_cast<por::event::wait2 const*>(event)->cid();
					break;
			}
			return result;
		}

		// IMPORTANT: comb must be conflict-free
		template<typename UnaryPredicate>
		std::vector<std::vector<por::event::event const*>> concurrent_combinations(
			std::map<por::event::thread_id_t, std::vector<por::event::event const*>> &comb,
			UnaryPredicate filter
		) {
			std::vector<std::vector<por::event::event const*>> result;
			// compute all combinations: S \subseteq comb (where S is concurrent,
			// i.e. there are no causal dependencies between any of its elements)
			assert(comb.size() < 64); // FIXME: can "only" be used with 64 threads
			for(std::uint64_t mask = 0; mask < (static_cast<std::uint64_t>(1) << comb.size()); ++mask) {
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
						std::vector<por::event::event const*> subset;
						subset.reserve(popcount);
						bool is_concurrent = true;
						for(std::size_t k = 0; k < popcount; ++k) {
							auto& new_event = comb[selected_threads[k]][event_indices[k]];
							if(k > 0) {
								// check if new event is concurrent to previous ones
								for(auto& e : subset) {
									if(e->is_less_than(*new_event) || new_event->is_less_than(*e)) {
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
					std::vector<por::event::event const*> empty;
					if(filter(empty)) {
						result.push_back(std::move(empty));
					}
				}
			}
			return result;
		}

		std::vector<conflicting_extension> cex_acquire(por::event::event const& e) {
			assert(e.kind() == por::event::event_kind::lock_acquire || e.kind() == por::event::event_kind::wait2);

			std::vector<conflicting_extension> result;

			// immediate causal predecessor on same thread
			por::event::event const* et = e.thread_predecessor();
			// maximal event concerning same lock in history of e
			por::event::event const* er = e.lock_predecessor();
			// maximal event concerning same lock in [et] (acq) or [et] \cup [es] (wait2)
			por::event::event const* em = er;
			// immediate successor of em / ep operating on same lock
			por::event::event const* conflict = &e;
			// signaling event (only for wait2)
			por::event::event const* es = nullptr;

			assert(et != nullptr);

			if(e.kind() == por::event::event_kind::lock_acquire) {
				while(em != nullptr && !em->is_less_than_eq(*et)) {
					// descend chain of lock events until em is in [et]
					conflict = em;
					em = em->lock_predecessor();
				}
			} else {
				assert(e.kind() == por::event::event_kind::wait2);
				es = static_cast<por::event::wait2 const*>(&e)->notifying_event();
				assert(es != nullptr);
				while(em != nullptr && !em->is_less_than_eq(*et) && !em->is_less_than(*es)) {
					// descend chain of lock events until em is in [et] \cup [es]
					conflict = em;
					em = em->lock_predecessor();
				}
			}

			if(em == er) {
				return {};
			}

			if(em == nullptr) {
				// (kind(em) == lock_release || kind(em) == wait1) is included in while loop below (with correct lock predecessor)
				assert(e.kind() == por::event::event_kind::lock_acquire); // wait2 must have a wait1 or release as predecessor
				result.emplace_back(por::event::lock_acquire::alloc(*_unfolding, e.tid(), *et, nullptr), *conflict);
				_unfolding->stats_inc_event_created(por::event::event_kind::lock_acquire);
			}

			assert(er != nullptr); // if er is nullptr, em == er, so we already returned
			por::event::event const* ep = er->lock_predecessor(); // lock events in K \ {r}
			conflict = er;
			while(ep != nullptr && (em == nullptr || !ep->is_less_than_eq(*em)) && (es == nullptr || !ep->is_less_than_eq(*es))) {
				if(ep->kind() == por::event::event_kind::lock_release || ep->kind() == por::event::event_kind::wait1 || ep->kind() == por::event::event_kind::lock_create) {
					if(e.kind() == por::event::event_kind::lock_acquire) {
						result.emplace_back(por::event::lock_acquire::alloc(*_unfolding, e.tid(), *et, ep), *conflict);
						_unfolding->stats_inc_event_created(por::event::event_kind::lock_acquire);
					} else {
						assert(e.kind() == por::event::event_kind::wait2);
						assert(ep->kind() != por::event::event_kind::lock_create);
						auto* w2 = static_cast<por::event::wait2 const*>(&e);
						result.emplace_back(por::event::wait2::alloc(*_unfolding, e.tid(), w2->cid(), *et, *ep, *es), *conflict);
						_unfolding->stats_inc_event_created(por::event::event_kind::wait2);
					}
				}
				conflict = ep;
				ep = ep->lock_predecessor();
			}

			return result;
		}

		std::vector<conflicting_extension> cex_wait1(por::event::event const& e) {
			assert(e.kind() == por::event::event_kind::wait1);

			std::vector<conflicting_extension> result;
			auto const* w1 = static_cast<por::event::wait1 const*>(&e);

			// immediate causal predecessor on same thread
			por::event::event const* et = e.thread_predecessor();

			// exclude condition variable create event from comb (if present)
			por::event::event const* cond_create = nullptr;

			std::map<por::event::thread_id_t, std::vector<por::event::event const*>> comb;
			for(auto& p : get_condition_variable_predecessors(&e)) {
				// cond predecessors contains exactly the bro and lost sig causes outside of [et] plus cond_create (if exists)
				if(p->kind() == por::event::event_kind::condition_variable_create) {
					cond_create = p;
				} else {
					assert(p->tid() != e.tid() && !p->is_less_than(*et));
					comb[p->tid()].push_back(p);
				}
			}

			// sort each tooth of comb
			for(auto& [tid, tooth] : comb) {
				std::sort(tooth.begin(), tooth.end(), [](auto& a, auto& b) {
					return a->is_less_than(*b);
				});
			}

			concurrent_combinations(comb, [&](auto const& M) {
				bool generate_conflict = false;

				por::cone cone(*et, cond_create, util::make_iterator_range<por::event::event const* const*>(M.data(), M.data() + M.size()));

				// check if [M] \cup [et] != [e] \setminus {e}
				// NOTE: lock predecessor is an event on the same thread
				assert(cone.size() <= e.cone().size());
				if(cone.size() != e.cone().size()) {
					generate_conflict = true;
				} else {
					for(auto& [tid, c] : e.cone()) {
						if(cone.at(tid)->is_less_than(*c)) {
							generate_conflict = true;
							break;
						}
					}
				}

				if(!generate_conflict)
					return false;

				// compute conflicts (minimal events from comb \setminus [M])
				// TODO: compute real minimum, not a superset
				std::vector<por::event::event const*> conflicts;
				auto conflict_comb = comb;
				for(auto& m : M) {
					auto& tooth = conflict_comb[m->tid()];
					if(tooth.size() == 1 || tooth.back()->depth() == m->depth()) {
						conflict_comb.erase(m->tid());
					} else {
						auto it = std::find(tooth.begin(), tooth.end(), m);
						assert(it != tooth.end());
						tooth.erase(tooth.begin(), std::next(it));
					}
				}
				for(auto& [tid, tooth] : conflict_comb) {
					// minimal event from this thread
					conflicts.emplace_back(tooth.front());
				}

				// lock predecessor is guaranteed to be in [et] (has to be an aquire/wait2 on the same thread)
				std::vector<por::event::event const*> N(M);
				if(cond_create) {
					N.push_back(cond_create);
				}
				result.emplace_back(por::event::wait1::alloc(*_unfolding, e.tid(), w1->cid(), *et, *e.lock_predecessor(), std::move(N)), std::move(conflicts));
				_unfolding->stats_inc_event_created(por::event::event_kind::wait1);
				return false; // result of concurrent_combinations not needed
			});

			return result;
		}

		static std::vector<por::event::event const*> outstanding_wait1(por::event::cond_id_t cid, por::cone const& cone) {
			// collect threads blocked on wait1 on cond cid
			std::vector<por::event::event const*> wait1s;
			for(auto& [tid, c] : cone) {
				if(c->kind() == por::event::event_kind::wait1) {
					if(get_cid(c) == cid)
						wait1s.emplace_back(c);
				}
			}

			if(wait1s.empty())
				return wait1s;

			// sort wait1 events s.t. the first element always has the minimum depth
			std::sort(wait1s.begin(), wait1s.end(), [](auto& a, auto& b) {
				return a->depth() < b->depth();
			});

			// remove those that have already been notified (so just their w2 event is missing in cone)
			for(auto& [tid, c] : cone) {
				for(auto const* e = c; e != nullptr; e = e->thread_predecessor()) {
					if(wait1s.empty())
						break; // no wait1s left

					if(e->depth() < wait1s.front()->depth())
						break; // none of the waits can be notified by a predecessor on this thread

					if(e->kind() == por::event::event_kind::signal) {
						auto const* sig = static_cast<por::event::signal const*>(e);
						if(sig->cid() != cid)
							continue;

						if(sig->is_lost())
							continue;

						auto wait = sig->wait_predecessor();
						wait1s.erase(std::remove(wait1s.begin(), wait1s.end(), wait), wait1s.end());
					} else if(e->kind() == por::event::event_kind::broadcast) {
						auto const* bro = static_cast<por::event::broadcast const*>(e);
						if(bro->cid() != cid)
							continue;

						if(bro->is_lost())
							continue;

						wait1s.erase(std::remove_if(wait1s.begin(), wait1s.end(), [&](auto& w) {
							for(auto& n : bro->wait_predecessors()) {
								if(n->tid() != w->tid())
									continue;

								if(n->depth() == w->depth())
									return true;
							}
							return false;
						}), wait1s.end());
					}
				}
			}

			return wait1s;
		}

		static std::vector<por::event::event const*> outstanding_wait1(por::event::cond_id_t cid, std::vector<por::event::event const*> events) {
			assert(!events.empty());
			if(events.size() == 1) {
				assert(events.front() != nullptr);
				return outstanding_wait1(cid, events.front()->cone());
			}

			por::cone cone(util::make_iterator_range<por::event::event const* const*>(events.data(), events.data() + events.size()));
			return outstanding_wait1(cid, cone);
		}

		std::vector<conflicting_extension> cex_notification(por::event::event const& e) {
			assert(e.kind() == por::event::event_kind::signal || e.kind() == por::event::event_kind::broadcast);

			std::vector<conflicting_extension> result;

			// immediate causal predecessor on same thread
			por::event::event const* et = e.thread_predecessor();

			// exclude condition variable create event from comb (if present)
			por::event::event const* cond_create = nullptr;

			// condition variable id
			por::event::cond_id_t cid = get_cid(&e);

			// calculate maximal event(s) in causes outside of [et]
			std::vector<por::event::event const*> max;
			{
				// compute comb
				std::map<por::event::thread_id_t, std::vector<por::event::event const*>> comb;
				for(auto& p : get_condition_variable_predecessors(&e)) {
					// cond predecessors contain all causes outside of [et]
						if(p->tid() == e.tid() || p->is_less_than(*et))
							continue; // cond_create and wait1s (because of their lock relation) can be in in [et]
					comb[p->tid()].push_back(p);
				}
				// derive maximum from comb
				for(auto& [tid, tooth] : comb) {
					auto x = std::max_element(tooth.begin(), tooth.end(), [](auto& a, auto& b) {
						return a->is_less_than(*b);
					});
					bool is_maximal_element = true;
					for(auto it = max.begin(); it != max.end();) {
						if((*it)->is_less_than(**x)) {
							it = max.erase(it);
						} else {
							if((*x)->is_less_than(**it))
								is_maximal_element = false;
							++it;
						}
					}
					if(is_maximal_element)
						max.push_back(*x);
				}
			}

			// calculate comb, containing all w1, sig, bro events on same cond outside of [et] \cup succ(e) (which contains e)
			// we cannot use cond predecessors here as these are not complete
			std::map<por::event::thread_id_t, std::vector<por::event::event const*>> comb;
			for(auto& thread_head : _thread_heads) {
				por::event::event const* pred = thread_head.second;
				do {
					if(pred->tid() == e.tid())
						break; // all events on this thread are either in [et] or succ(e)

					if(e.is_less_than(*pred))
						break; // pred and all its predecessors are in succ(e)

					if(pred->is_less_than(*et))
						break; // pred and all its predecessors are in [et]

					if(get_cid(pred) == cid) {
						// only include events on same cond
						if(pred->kind() == por::event::event_kind::condition_variable_create) {
							cond_create = pred; // exclude from comb
						} else if(pred->kind() != por::event::event_kind::wait2) {
							// also exclude wait2 events
							comb[pred->tid()].push_back(pred);
						}
					}

					pred = pred->thread_predecessor();
				} while(pred != nullptr);
			}

			// prepare wait1_comb (sorted version of comb with only wait1 events on same cond)
			auto wait1_comb = comb;
			for(auto& [tid, tooth] : wait1_comb) {
				tooth.erase(std::remove_if(tooth.begin(), tooth.end(), [](auto& e) {
					return e->kind() != por::event::event_kind::wait1;
				}), tooth.end());
				std::sort(tooth.begin(), tooth.end(), [](auto& a, auto& b) {
					return a->is_less_than(*b);
				});
			}

			// conflicting extensions: lost notification events
			concurrent_combinations(comb, [&](auto const& M) {
				// ensure that M differs from max
				if(max.size() == M.size()) {
					bool ismax = true;
					for(auto& m : M) {
						for(auto& x : max) {
							if(m->is_less_than(*x)) {
								// at least one element of M is smaller than some element of max
								ismax = false;
								break;
							}
						}
					}
					if(ismax)
						return false;
				}

				// ensure that there are only non-lost notifications in M
				if(M.size() == 1 && M.front()->kind() == por::event::event_kind::broadcast) {
					auto* bro = static_cast<por::event::broadcast const*>(M.front());
					if(bro->is_lost())
						return false;
				} else {
					for(auto& m : M) {
						if(m->kind() != por::event::event_kind::signal)
							return false;
						auto* sig = static_cast<por::event::signal const*>(m);
						if(sig->is_lost())
							return false;
					}
				}

				// ensure that there are no outstanding wait1s on same condition variable
				std::vector<por::event::event const*> M_et(M.begin(), M.end());
				M_et.reserve(M.size() + 1);
				M_et.push_back(et);

				if(!outstanding_wait1(cid, M_et).empty())
					return false;

				// create set of cond predecessors
				std::vector<por::event::event const*> N;
				for(auto& m : M) {
					// NOTE: M already contains only events that are not in [et]
					if(m->kind() == por::event::event_kind::broadcast) {
						auto bro = static_cast<por::event::broadcast const*>(m);
						if(bro->is_lost())
							continue;

						if(bro->is_notifying_thread(e.tid()))
							continue;
					} else if(m->kind() == por::event::event_kind::signal) {
						auto sig = static_cast<por::event::signal const*>(m);
						if(sig->is_lost())
							continue;

						if(sig->notified_thread() == e.tid())
							continue;
					} else {
						continue; // exclude all other events
					}
					N.push_back(m);
				}
				if(cond_create) {
					N.push_back(cond_create);
				}

				// compute conflicts (minimal event from {e} or wait1s in (comb \setminus [M]))
				// TODO: compute real minimum, not a superset
				std::vector<por::event::event const*> conflicts;
				auto conflict_comb = wait1_comb;
				{ // add e to conflict_comb and sort the corresponding tooth
					auto& e_tooth = conflict_comb[e.tid()];
					e_tooth.push_back(&e);
					std::sort(e_tooth.begin(), e_tooth.end(), [](auto& a, auto& b) {
						return a->is_less_than(*b);
					});
				}
				for(auto& m : M) {
					auto it = conflict_comb.begin();
					while(it != conflict_comb.end()) {
						auto& tooth = it->second;
						tooth.erase(std::remove_if(tooth.begin(), tooth.end(), [&](auto& e) {
							return m->is_less_than_eq(*e);
						}), tooth.end());
						if(tooth.empty())
							it = conflict_comb.erase(it);
						else
							++it;
					}
				}
				for(auto& [tid, tooth] : conflict_comb) {
					if(tooth.empty())
						continue;

					// minimal event from this thread
					conflicts.emplace_back(tooth.front());
				}

				if(e.kind() == por::event::event_kind::signal) {
					result.emplace_back(por::event::signal::alloc(*_unfolding, e.tid(), cid, *et, std::move(N)), std::move(conflicts));
					_unfolding->stats_inc_event_created(por::event::event_kind::signal);
				} else if(e.kind() == por::event::event_kind::broadcast) {
					result.emplace_back(por::event::broadcast::alloc(*_unfolding, e.tid(), cid, *et, std::move(N)), std::move(conflicts));
					_unfolding->stats_inc_event_created(por::event::event_kind::broadcast);
				}

				return false; // result of concurrent_combinations not needed
			});

			// conflicting extensions: signal events
			if(e.kind() == por::event::event_kind::signal) {
				auto sig = static_cast<por::event::signal const*>(&e);

				// generate set W with all wait1 events in comb and those that are outstanding in [et]
				std::vector<por::event::event const*> W = outstanding_wait1(cid, et->cone());
				for(auto& [tid, tooth] : wait1_comb) {
					W.reserve(W.size() + tooth.size());
					for(auto& e : tooth) {
						W.push_back(e);
					}
				}

				// compute map from each w1 to its (non-lost) notification in comb
				std::map<por::event::event const*, por::event::event const*> notification;
				for(auto& [tid, tooth] : comb) {
					for(auto& n : tooth) {
						if(n->kind() != por::event::event_kind::signal && n->kind() != por::event::event_kind::broadcast)
							continue;

						if(n->kind() == por::event::event_kind::signal) {
							auto sig = static_cast<por::event::signal const*>(n);
							if(sig->is_lost())
								continue;

							notification.emplace(sig->wait_predecessor(), n);
						}

						if(n->kind() == por::event::event_kind::broadcast) {
							auto bro = static_cast<por::event::broadcast const*>(n);
							if(bro->is_lost())
								continue;

							for(auto& w : bro->wait_predecessors()) {
								notification.emplace(w, n);
							}
						}
					}
				}

				for(auto& w : W) {
					if(w == sig->wait_predecessor())
						continue;

					// compute conflicts
					std::vector<por::event::event const*> conflicts;
					conflicts.push_back(&e);
					// because of e \in conflicts, we only need to search comb for notification
					// (succ(e) is excluded and w cannot be woken up by an event in [et])
					if(notification.count(w))
						conflicts.push_back(notification.at(w));

					result.emplace_back(por::event::signal::alloc(*_unfolding, e.tid(), cid, *et, *w), std::move(conflicts));
					_unfolding->stats_inc_event_created(por::event::event_kind::signal);
				}
			}

			// conflicting extensions: broadcast events
			if(e.kind() == por::event::event_kind::broadcast) {
				// compute pre-filtered conflict comb (only containing wait1, non-lost sig and lost bro events)
				auto broadcast_conflict_comb = comb;
				for(auto& [tid, tooth] : broadcast_conflict_comb) {
					tooth.erase(std::remove_if(tooth.begin(), tooth.end(), [](auto& e) {
						if(e->kind() == por::event::event_kind::wait1) {
							return false;
						} else if(e->kind() == por::event::event_kind::signal) {
							auto* sig = static_cast<por::event::signal const*>(e);
							if(!sig->is_lost())
								return false;
						} else if(e->kind() == por::event::event_kind::broadcast) {
							auto* bro = static_cast<por::event::broadcast const*>(e);
							if(bro->is_lost())
								return false;
						}
						return true;
					}), tooth.end());
					std::sort(tooth.begin(), tooth.end(), [](auto& a, auto& b) {
						return a->is_less_than(*b);
					});
				}

				concurrent_combinations(comb, [&](auto const& M) {
					// ensure that M differs from max
					if(max.size() == M.size()) {
						bool ismax = true;
						for(auto& m : M) {
							for(auto& x : max) {
								if(m->is_less_than(*x)) {
									// at least one element of M is smaller than some element of max
									ismax = false;
									break;
								}
							}
						}
						if(ismax)
							return false;
					}

					// ensure that M only contains non-lost signal and wait1 events
					for(auto& m : M) {
						if(m->kind() == por::event::event_kind::wait1)
							continue;

						if(m->kind() == por::event::event_kind::signal) {
							auto* sig = static_cast<por::event::signal const*>(m);
							if(sig->is_lost())
								return false;
						}

						return false;
					}

					// ensure that there are outstanding wait1s on same condition variable
					std::vector<por::event::event const*> M_et(M.begin(), M.end());
					M_et.reserve(M.size() + 1);
					M_et.push_back(et);

					auto outstanding = outstanding_wait1(cid, M_et);
					if(outstanding.empty())
						return false;

					// create set of cond predecessors (which is exactly M)
					// because here, it is guaranteed that contained signals do not notify any of the wait1s
					std::vector<por::event::event const*> N;
					N.reserve(M.size());
					for(auto& m : M) {
						N.push_back(m);
					}

					// compute conflicts
					// TODO: compute real minimum, not a superset
					std::vector<por::event::event const*> conflicts;
					auto conflict_comb = broadcast_conflict_comb;
					for(auto& m : M) {
						auto it = conflict_comb.begin();
						while(it != conflict_comb.end()) {
							auto& tooth = it->second;
							tooth.erase(std::remove_if(tooth.begin(), tooth.end(), [&](auto& e) {
								return m->is_less_than_eq(*e);
							}), tooth.end());
							if(tooth.empty())
								it = conflict_comb.erase(it);
							else
								++it;
						}
					}
					for(auto& [tid, tooth] : conflict_comb) {
						if(tooth.empty())
							continue;

						// minimal event from this thread
						conflicts.emplace_back(tooth.front());
					}
					// as long as this is true, e does not have to be included itself
					//assert(!conflicts.empty()); // FIXME: does fail for random-graph 144

					// FIXME: conflicts and causes are incorrect!
					// result.emplace_back(por::event::broadcast::alloc(e->tid(), cid, *et, N.data(), N.data() + N.size()), std::move(conflicts));
					// _unfolding->stats_inc_event_created(por::event::event_kind::broadcast);
					return false; // result of concurrent_combinations not needed
				});
			}

			return result;
		}

		// returns index of first conflict, i.e. first index that deviates from given schedule
		std::size_t compute_new_schedule_from_old(conflicting_extension const& cex, std::vector<por::event::event const*>& schedule) {
			[[maybe_unused]] std::size_t original_size = schedule.size();
			auto is_in_conflict = [&](auto& e) {
				for(auto& conflict : cex.conflicts()) {
					assert(conflict);
					assert(e);
					if((*conflict).is_less_than_eq(*e))
						return true;
				}
				return false;
			};
			auto first_conflict = std::find_if(schedule.begin(), schedule.end(), is_in_conflict);
			schedule.erase(std::remove_if(first_conflict, schedule.end(), is_in_conflict), schedule.end());
			assert(schedule.size() <= (original_size - cex.num_of_conflicts()));
			schedule.emplace_back(&cex.new_event());
			return std::distance(schedule.begin(), first_conflict);
		}

		std::pair<std::vector<por::event::event const*>, std::size_t> compute_new_schedule_from_current(conflicting_extension const& cex) {
			std::vector<por::event::event const*> result = _schedule;
			std::size_t prefix = compute_new_schedule_from_old(cex, result);
			return std::make_pair(result, prefix);
		}

	public:
		std::vector<conflicting_extension> conflicting_extensions() {
			_unfolding->stats_inc_configuration();
			std::vector<conflicting_extension> S;
			por::event::path_t path = _path;
			for(auto it = _schedule.rbegin(), ie = _schedule.rend(); it != ie; ++it) {
				por::event::event const* e = *it;
				if(!_unfolding->is_explored(*e, path)) {
					std::vector<conflicting_extension> candidates;
					switch(e->kind()) {
						case por::event::event_kind::lock_acquire:
						case por::event::event_kind::wait2:
							candidates = cex_acquire(*e);
							break;
						case por::event::event_kind::wait1:
							candidates = cex_wait1(*e);
							break;
						case por::event::event_kind::signal:
						case por::event::event_kind::broadcast:
							candidates = cex_notification(*e);
							break;
					}
					_unfolding->stats_inc_cex_candidates(candidates.size());
					for(auto& cex : candidates) {
						// compute path for cex
						auto cex_schedule = compute_new_schedule_from_current(cex).first;
						por::event::path_t cex_path;
						for(auto& event : cex_schedule) {
							if(event->kind() == por::event::event_kind::local) {
								auto local = static_cast<por::event::local const*>(event);
								auto& local_path = local->path();
								cex_path.insert(cex_path.end(), local_path.begin(), local_path.end());
							}
						}

						if(_unfolding->is_present(cex.new_event(), cex_path))
							continue;
						S.emplace_back(std::move(cex));
					}
					_unfolding->mark_as_explored(*e, path);
				}
				// remove choices made in local events from path for previous ones
				// (as we are iterating in reverse)
				if(e->kind() == por::event::event_kind::local) {
					auto local = static_cast<por::event::local const*>(e);
					for(std::size_t i = 0; i < local->path().size(); ++i) {
						assert(!path.empty());
						path.pop_back();
					}
				}
			}
			_unfolding->stats_inc_cex_created(S.size());
			return S;
		}
	};

	inline configuration configuration_root::construct() { return configuration(std::move(*this)); }
}
