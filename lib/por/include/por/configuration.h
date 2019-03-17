#pragma once

#include "event/event.h"

#include <cassert>
#include <map>
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
		// contains most recent event of ALL threads that ever existed within this configuration
		std::map<por::event::thread_id_t, std::shared_ptr<event::event>> _thread_heads;

		// contains most recent event of ACTIVE locks
		std::map<event::lock_id_t, std::shared_ptr<event::event>> _lock_heads;

		// contains most recent event of ACTIVE condition variables for each thread
		std::map<por::event::cond_id_t, std::vector<std::shared_ptr<por::event::event>>> _cond_heads;

	public:
		configuration() : configuration(configuration_root{}.add_thread().construct()) { }
		configuration(configuration const&) = default;
		configuration& operator=(configuration const&) = delete;
		configuration(configuration&&) = default;
		configuration& operator=(configuration&&) = default;
		configuration(configuration_root&& root)
			: _thread_heads(std::move(root._thread_heads))
		{
			assert(!_thread_heads.empty() && "Cannot create a configuration without any startup threads");
		}

		auto const& thread_heads() const noexcept { return _thread_heads; }
		auto const& lock_heads() const noexcept { return _lock_heads; }
		auto const& cond_heads() const noexcept { return _cond_heads; }

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

			source_event = event::thread_create::alloc(source, std::move(source_event));
			assert(new_tid > 0);
			assert(thread_heads().find(new_tid) == thread_heads().end() && "Thread with same id already exists");
			_thread_heads.emplace(new_tid, event::thread_init::alloc(new_tid, source_event));
		}

		void join_thread(event::thread_id_t thread, event::thread_id_t joined) {
			auto thread_it = _thread_heads.find(thread);
			assert(thread_it != _thread_heads.end() && "Thread must exist");
			auto& thread_event = thread_it->second;
			assert(thread_event->kind() != por::event::event_kind::thread_exit && "Thread must not yet be exited");
			auto joined_it = _thread_heads.find(joined);
			assert(joined_it != _thread_heads.end() && "Joined thread must exist");
			auto& joined_event = joined_it->second;
			assert(joined_event->kind() == por::event::event_kind::thread_exit && "Joined thread must be exited");

			thread_event = event::thread_join::alloc(thread, std::move(thread_event), joined_event);
		}

		void exit_thread(event::thread_id_t thread) {
			auto thread_it = _thread_heads.find(thread);
			assert(thread_it != _thread_heads.end() && "Thread must exist");
			auto& thread_event = thread_it->second;
			assert(thread_event->kind() != por::event::event_kind::thread_exit && "Thread must not yet be exited");

			assert(active_threads() > 0);
			thread_event = event::thread_exit::alloc(thread, std::move(thread_event));
		}

		void create_lock(event::thread_id_t thread, event::lock_id_t lock) {
			auto thread_it = _thread_heads.find(thread);
			assert(thread_it != _thread_heads.end() && "Thread must exist");
			auto& thread_event = thread_it->second;
			assert(thread_event->kind() != por::event::event_kind::thread_exit && "Thread must not yet be exited");

			assert(lock > 0);
			assert(_lock_heads.find(lock) == _lock_heads.end() && "Lock id already taken");

			thread_event = event::lock_create::alloc(thread, std::move(thread_event));
			_lock_heads.emplace(lock, thread_event);
		}

		void destroy_lock(event::thread_id_t thread, event::lock_id_t lock) {
			auto thread_it = _thread_heads.find(thread);
			assert(thread_it != _thread_heads.end() && "Thread must exist");
			auto& thread_event = thread_it->second;
			assert(thread_event->kind() != por::event::event_kind::thread_exit && "Thread must not yet be exited");
			auto lock_it = _lock_heads.find(lock);
			assert(_lock_heads.find(lock) != _lock_heads.end() && "Lock must (still) exist");

			thread_event = event::lock_destroy::alloc(thread, std::move(thread_event), std::move(lock_it->second));
			_lock_heads.erase(lock_it);
		}

		void acquire_lock(event::thread_id_t thread, event::lock_id_t lock) {
			auto thread_it = _thread_heads.find(thread);
			assert(thread_it != _thread_heads.end() && "Thread must exist");
			auto& thread_event = thread_it->second;
			assert(thread_event->kind() != por::event::event_kind::thread_exit && "Thread must not yet be exited");
			auto lock_it = _lock_heads.find(lock);
			assert(_lock_heads.find(lock) != _lock_heads.end() && "Lock must (still) exist");

			auto& lock_event = lock_it->second;
			thread_event = event::lock_acquire::alloc(thread, std::move(thread_event), std::move(lock_event));
			lock_event = thread_event;
		}

		void release_lock(event::thread_id_t thread, event::lock_id_t lock) {
			auto thread_it = _thread_heads.find(thread);
			assert(thread_it != _thread_heads.end() && "Thread must exist");
			auto& thread_event = thread_it->second;
			assert(thread_event->kind() != por::event::event_kind::thread_exit && "Thread must not yet be exited");
			auto lock_it = _lock_heads.find(lock);
			assert(_lock_heads.find(lock) != _lock_heads.end() && "Lock must (still) exist");

			auto& lock_event = lock_it->second;
			thread_event = event::lock_release::alloc(thread, std::move(thread_event), std::move(lock_event));
			lock_event = thread_event;
		}

		void create_cond(por::event::thread_id_t thread, por::event::cond_id_t cond) {
			auto thread_it = _thread_heads.find(thread);
			assert(thread_it != _thread_heads.end() && "Thread must exist");
			auto& thread_event = thread_it->second;
			assert(thread_event->kind() != por::event::event_kind::thread_exit && "Thread must not yet be exited");
			assert(cond > 0);
			assert(_cond_heads.find(cond) == _cond_heads.end() && "Condition variable id already taken");

			thread_event = por::event::condition_variable_create::alloc(thread, std::move(thread_event));
			_cond_heads.emplace(cond, std::vector{thread_event});
		}

		void destroy_cond(por::event::thread_id_t thread, por::event::cond_id_t cond) {
			auto thread_it = _thread_heads.find(thread);
			assert(thread_it != _thread_heads.end() && "Thread must exist");
			auto& thread_event = thread_it->second;
			assert(thread_event->kind() != por::event::event_kind::thread_exit && "Thread must not yet be exited");
			auto cond_head_it = _cond_heads.find(cond);
			assert(cond_head_it != _cond_heads.end() && "Condition variable must (still) exist");
			auto& cond_preds = cond_head_it->second;
			assert(cond_preds.size() > 0);

			thread_event = por::event::condition_variable_destroy::alloc(thread, std::move(thread_event), cond_preds.data(), cond_preds.data() + cond_preds.size());
			_cond_heads.erase(cond_head_it);
		}

		void local(event::thread_id_t thread) {
			auto thread_it = _thread_heads.find(thread);
			assert(thread_it != _thread_heads.end() && "Thread must exist");
			auto& thread_event = thread_it->second;
			assert(thread_event->kind() != por::event::event_kind::thread_exit && "Thread must not yet be exited");

			thread_event = event::local::alloc(thread, std::move(thread_event));
		}

	private:
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

			assert(er != nullptr);
			std::shared_ptr<por::event::event> const* ep = get_lock_predecessor(*er);
			do {
				assert(ep != nullptr);
				if((*ep)->kind() == por::event::event_kind::lock_release || (*ep)->kind() == por::event::event_kind::wait1) {
					result.emplace_back(por::event::lock_acquire::alloc(e->tid(), *et, *ep));
				}
				ep = get_lock_predecessor(*ep);
			} while(ep != nullptr && (*em < *ep || em == ep));

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
