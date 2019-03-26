#pragma once

#include "event/event.h"

#include <cassert>
#include <map>
#include <set>
#include <vector>

namespace por {
	class configuration;

	class configuration_root {
		friend class configuration;

		std::shared_ptr<por::event::program_init> _program_init = event::program_init::alloc();

		std::map<por::event::thread_id_t, std::shared_ptr<event::event>> _thread_heads;
		por::event::thread_id_t _next_thread = 1;

	public:
		configuration construct();

		configuration_root& add_thread() {
			auto const tid = _next_thread++;
			assert(tid > 0);

			_thread_heads.emplace(tid, event::thread_init::alloc(tid, _program_init));

			return *this;
		}
	};

	class configuration {
		// creation events for locks and condition variables are optional
		static const bool optional_creation_events = true;

		// contains most recent event of ALL threads that ever existed within this configuration
		std::map<por::event::thread_id_t, std::shared_ptr<event::event>> _thread_heads;

		// contains most recent event of ACTIVE locks
		std::map<event::lock_id_t, std::shared_ptr<event::event>> _lock_heads;

		// contains most recent event of ACTIVE condition variables for each thread
		std::map<por::event::cond_id_t, std::vector<std::shared_ptr<por::event::event>>> _cond_heads;

		// sequence of events in order of their execution
		std::vector<std::shared_ptr<por::event::event>> _schedule;

	public:
		configuration() : configuration(configuration_root{}.add_thread().construct()) { }
		configuration(configuration const&) = default;
		configuration& operator=(configuration const&) = delete;
		configuration(configuration&&) = default;
		configuration& operator=(configuration&&) = default;
		configuration(configuration_root&& root)
			: _thread_heads(std::move(root._thread_heads))
		{
			_schedule.emplace_back(root._program_init);
			for(auto& thread : _thread_heads) {
				_schedule.emplace_back(thread.second);
			}
			assert(!_thread_heads.empty() && "Cannot create a configuration without any startup threads");
		}

		auto const& thread_heads() const noexcept { return _thread_heads; }
		auto const& lock_heads() const noexcept { return _lock_heads; }
		auto const& cond_heads() const noexcept { return _cond_heads; }

		auto const& schedule() const noexcept { return _schedule; }

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
		}

		void join_thread(event::thread_id_t thread, event::thread_id_t joined) {
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
		}

		void exit_thread(event::thread_id_t thread) {
			auto thread_it = _thread_heads.find(thread);
			assert(thread_it != _thread_heads.end() && "Thread must exist");
			auto& thread_event = thread_it->second;
			assert(thread_event->kind() != por::event::event_kind::thread_exit && "Thread must not yet be exited");
			assert(thread_event->kind() != por::event::event_kind::wait1 && "Thread must not be blocked");

			assert(active_threads() > 0);
			thread_event = event::thread_exit::alloc(thread, std::move(thread_event));

			_schedule.emplace_back(thread_event);
		}

		void create_lock(event::thread_id_t thread, event::lock_id_t lock) {
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
		}

		void destroy_lock(event::thread_id_t thread, event::lock_id_t lock) {
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
					return;
				}
			}
			assert(lock_it != _lock_heads.end() && "Lock must (still) exist");
			auto& lock_event = lock_it->second;
			thread_event = event::lock_destroy::alloc(thread, std::move(thread_event), std::move(lock_event));
			_lock_heads.erase(lock_it);

			_schedule.emplace_back(thread_event);
		}

		void acquire_lock(event::thread_id_t thread, event::lock_id_t lock) {
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
					return;
				}
			}
			assert(lock_it != _lock_heads.end() && "Lock must (still) exist");
			auto& lock_event = lock_it->second;
			thread_event = event::lock_acquire::alloc(thread, std::move(thread_event), std::move(lock_event));
			lock_event = thread_event;

			_schedule.emplace_back(thread_event);
		}

		void release_lock(event::thread_id_t thread, event::lock_id_t lock) {
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
					return;
				}
			}
			assert(lock_it != _lock_heads.end() && "Lock must (still) exist");
			auto& lock_event = lock_it->second;
			thread_event = event::lock_release::alloc(thread, std::move(thread_event), std::move(lock_event));
			lock_event = thread_event;

			_schedule.emplace_back(thread_event);
		}

		void create_cond(por::event::thread_id_t thread, por::event::cond_id_t cond) {
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
		}

		void destroy_cond(por::event::thread_id_t thread, por::event::cond_id_t cond) {
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
					return;
				}
			}
			assert(cond_head_it != _cond_heads.end() && "Condition variable must (still) exist");
			auto& cond_preds = cond_head_it->second;
			assert(cond_preds.size() > 0);

			thread_event = por::event::condition_variable_destroy::alloc(thread, std::move(thread_event), cond_preds.data(), cond_preds.data() + cond_preds.size());
			_cond_heads.erase(cond_head_it);

			_schedule.emplace_back(thread_event);
		}

		void wait1(por::event::thread_id_t thread, por::event::cond_id_t cond, por::event::lock_id_t lock) {
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
					return;
				}
			}
			assert(cond_head_it != _cond_heads.end() && "Condition variable must (still) exist");
			auto& cond_preds = cond_head_it->second;
			assert(lock_it != _lock_heads.end() && "Lock must (still) exist");
			auto& lock_event = lock_it->second;

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

			thread_event = por::event::wait1::alloc(thread, std::move(thread_event), std::move(lock_event), non_waiting.data(), non_waiting.data() + non_waiting.size());
			lock_event = thread_event;
			cond_preds.push_back(thread_event);

			_schedule.emplace_back(thread_event);
		}

		void wait2(por::event::thread_id_t thread, por::event::cond_id_t cond, por::event::lock_id_t lock) {
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

			for(auto& e : cond_preds) {
				if(e->kind() == por::event::event_kind::signal || e->kind() == por::event::event_kind::broadcast) {
					// TODO: make search more efficient
					if(std::find(e->predecessors().begin(), e->predecessors().end(), thread_event) != e->predecessors().end()) {
						thread_event = por::event::wait2::alloc(thread, std::move(thread_event), std::move(lock_event), e);
						lock_event = thread_event;

						_schedule.emplace_back(thread_event);
						return;
					}
				}
			}
			assert(0 && "There has to be a notifying event before a wait2");
		}

		void signal_thread(por::event::thread_id_t thread, por::event::cond_id_t cond, por::event::thread_id_t notified_thread) {
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
					return;
				}
			}
			assert(cond_head_it != _cond_heads.end() && "Condition variable must (still) exist");
			auto& cond_preds = cond_head_it->second;

			if(notified_thread == 0) { // lost signal
				std::vector<std::shared_ptr<por::event::event>> prev_notifications;
				for(auto it = cond_preds.begin(); it != cond_preds.end(); ++it) {
					if((*it)->kind() == por::event::event_kind::wait1)
						continue;

					if((*it)->tid() == thread)
						continue; // excluded event is part of [thread_event]

					if((*it)->kind() == por::event::event_kind::broadcast) {
						auto bro = static_cast<por::event::broadcast const*>(it->get());
						if(bro->is_lost())
							continue;

						if(bro->is_notifying_thread(thread))
							continue;
					}

					if((*it)->kind() == por::event::event_kind::signal) {
						auto sig = static_cast<por::event::signal const*>(it->get());
						if(sig->is_lost())
							continue;

						if(sig->notified_thread() == thread)
							continue; // excluded event notified current thread by signal
					}

					prev_notifications.push_back(*it);
				}

				thread_event = por::event::signal::alloc(thread, std::move(thread_event), prev_notifications.data(), prev_notifications.data() + prev_notifications.size());
				cond_preds.push_back(thread_event);
			} else { // notifying signal
				assert(notified_thread != thread && "Thread cannot notify itself");
				auto notified_thread_it = _thread_heads.find(notified_thread);
				assert(notified_thread_it != _thread_heads.end() && "Notified thread must exist");
				auto& notified_thread_event = notified_thread_it->second;
				assert(notified_thread_event->kind() != por::event::event_kind::thread_exit && "Thread must not yet be exited");
				assert(notified_thread_event->kind() == por::event::event_kind::wait1 && "Notified thread must be waiting");

				auto cond_it = std::find_if(cond_preds.begin(), cond_preds.end(), [&notified_thread](auto& e) {
					return e->tid() == notified_thread && e->kind() == por::event::event_kind::wait1;
				});
				assert(cond_it != cond_preds.end() && "Wait1 event must be in cond_heads");
				auto& cond_event = *cond_it;
				assert(cond_event == notified_thread_event);

				thread_event = por::event::signal::alloc(thread, std::move(thread_event), std::move(cond_event));
				cond_event = thread_event;
			}

			_schedule.emplace_back(thread_event);
		}

		void broadcast_threads(por::event::thread_id_t thread, por::event::cond_id_t cond, std::set<por::event::thread_id_t> notified_threads) {
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
					return;
				}
			}
			assert(cond_head_it != _cond_heads.end() && "Condition variable must (still) exist");
			auto& cond_preds = cond_head_it->second;

			if(notified_threads.empty()) { // lost broadcast
				std::vector<std::shared_ptr<por::event::event>> prev_notifications;
				for(auto it = cond_preds.begin(); it != cond_preds.end(); ++it) {
					if((*it)->kind() == por::event::event_kind::wait1)
						continue;

					if((*it)->tid() == thread)
						continue; // excluded event is part of [thread_event]

					if((*it)->kind() == por::event::event_kind::broadcast) {
						auto bro = static_cast<por::event::broadcast const*>(it->get());
						if(bro->is_lost())
							continue;

						if(bro->is_notifying_thread(thread))
							continue;
					}

					if((*it)->kind() == por::event::event_kind::signal) {
						auto sig = static_cast<por::event::signal const*>(it->get());
						if(sig->is_lost())
							continue;

						if(sig->notified_thread() == thread)
							continue;
					}

					prev_notifications.push_back(*it);
				}

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

					auto cond_it = std::find_if(cond_preds.begin(), cond_preds.end(), [&nid](auto& e) {
						return e->tid() == nid && e->kind() == por::event::event_kind::wait1;
					});
					assert(cond_it != cond_preds.end() && "Wait1 event must be in cond_heads");
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

						if(notified_threads.count(sig->notified_thread()))
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
		}

		void local(event::thread_id_t thread) {
			auto thread_it = _thread_heads.find(thread);
			assert(thread_it != _thread_heads.end() && "Thread must exist");
			auto& thread_event = thread_it->second;
			assert(thread_event->kind() != por::event::event_kind::thread_exit && "Thread must not yet be exited");

			thread_event = event::local::alloc(thread, std::move(thread_event));

			_schedule.emplace_back(thread_event);
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
									if(**e < **new_event || **new_event < **e) {
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

		std::vector<std::shared_ptr<por::event::event>> cex_acquire(std::shared_ptr<por::event::event> const& e) {
			assert(e->kind() == por::event::event_kind::lock_acquire || e->kind() == por::event::event_kind::wait2);

			std::vector<std::shared_ptr<por::event::event>> result;

			// immediate causal predecessor on same thread
			std::shared_ptr<por::event::event> const* et = get_thread_predecessor(e);
			// maximal event concerning same lock in history of e
			std::shared_ptr<por::event::event> const* er = get_lock_predecessor(e);
			// maximal event concerning same lock in [et]
			std::shared_ptr<por::event::event> const* em = er;
			// signaling event (only for wait2)
			std::shared_ptr<por::event::event> const* es = nullptr;

			assert(et != nullptr);

			if(e->kind() == por::event::event_kind::lock_acquire) {
				while(em != nullptr && *et < *em) {
					// descend chain of lock events until em is in [et]
					em = get_lock_predecessor(*em);
				}
			} else {
				assert(e->kind() == por::event::event_kind::wait2);
				auto* w2 = static_cast<por::event::wait2 const*>(e.get());
				es = &w2->condition_variable_predecessor();
				assert(es != nullptr && *es != nullptr);
				while(em != nullptr && *et < *em && *es < *em) {
					// descend chain of lock events until em is in [et] \cup [es]
					em = get_lock_predecessor(*em);
				}
			}

			if(em == er) {
				return {};
			}

			if(em == nullptr) {
				result.emplace_back(por::event::lock_acquire::alloc(e->tid(), *et, nullptr));
			}

			assert(er != nullptr); // if er is nullptr, em == er, so we already returned
			std::shared_ptr<por::event::event> const* ep = get_lock_predecessor(*er);
			while(ep != nullptr && em != nullptr && (*em < *ep || em == ep)) {
				if((*ep)->kind() == por::event::event_kind::lock_release || (*ep)->kind() == por::event::event_kind::wait1) {
					if(e->kind() == por::event::event_kind::lock_acquire) {
						result.emplace_back(por::event::lock_acquire::alloc(e->tid(), *et, *ep));
					} else {
						assert(e->kind() == por::event::event_kind::wait2);
						result.emplace_back(por::event::wait2::alloc(e->tid(), *et, *ep, *es));
					}
				}
				ep = get_lock_predecessor(*ep);
			}

			return result;
		}

	public:
		std::vector<std::shared_ptr<por::event::event>> conflicting_extensions() {
			std::vector<std::shared_ptr<por::event::event>> S;
			for(auto& t : _thread_heads) {
				std::shared_ptr<por::event::event> const* e = &t.second;
				do {
					switch((*e)->kind()) {
						case por::event::event_kind::lock_acquire:
						case por::event::event_kind::wait2: {
							auto r = cex_acquire(*e);
							S.insert(S.end(), r.begin(), r.end());
							break;
						}
					}
					bool has_predecessor = false;
					auto p = get_thread_predecessor(*e);
					if(p != nullptr && p != e && (*p)->tid() == t.first) {
						// in lieu of a more sophisticated set of visited events, stop when entering another thread
						e = p;
						has_predecessor = true;
					}
					if(!has_predecessor) e = nullptr;
				} while(e != nullptr);
			}
			return S;
		}
	};

	inline configuration configuration_root::construct() { return configuration(std::move(*this)); }
}
