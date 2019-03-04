#pragma once

#include "event/event.h"

#include <cassert>
#include <map>

namespace por {
	class configuration;

	class configuration_builder{
		friend class configuration;

		std::shared_ptr<por::event::program_init> _program_init = event::program_init::alloc();

		std::map<por::event::thread_id_t, std::shared_ptr<event::event>> _thread_heads;
		por::event::thread_id_t _next_thread = 1;

		std::map<event::lock_id_t, std::shared_ptr<event::event>> _lock_heads;
		por::event::lock_id_t _next_lock = 1;

	public:
		configuration construct();

		configuration_builder& add_thread() {
			auto const tid = _next_thread++;
			assert(tid > 0);

			_thread_heads.emplace(tid, event::thread_init::alloc(tid, _program_init));

			return *this;
		}
	};

	class configuration {
		std::map<por::event::thread_id_t, std::shared_ptr<event::event>> _thread_heads;
		por::event::thread_id_t _next_thread;
		por::event::thread_id_t _active_threads; // really optimizes for a case we will not care about outside of simulations

		std::map<event::lock_id_t, std::shared_ptr<event::event>> _lock_heads;
		por::event::lock_id_t _next_lock;

	public:
		configuration() : configuration(configuration_builder{}.add_thread().construct()) { }
		configuration(configuration const&) = delete;
		configuration& operator=(configuration const&) = delete;
		configuration(configuration&&) = default;
		configuration& operator=(configuration&&) = default;
		configuration(configuration_builder&& builder)
			: _thread_heads(std::move(builder._thread_heads))
			, _next_thread(std::move(builder._next_thread))
			, _active_threads(_thread_heads.size())
			, _lock_heads(std::move(builder._lock_heads))
			, _next_lock(std::move(builder._next_lock))
		{
			assert(!_thread_heads.empty() && "Cannot create a configuration without any startup threads");
			assert(_next_thread > 0);
			assert(_active_threads > 0);
			assert(_next_lock > 0);
		}

		auto const& thread_heads() const noexcept { return _thread_heads; }
		auto const& lock_heads() const noexcept { return _lock_heads; }

		por::event::thread_id_t const& active_threads() const noexcept { return _active_threads; }

		// Spawn a new thread from tid `source`.
		por::event::thread_id_t spawn_thread(event::thread_id_t source) {
			auto source_it = _thread_heads.find(source);
			assert(source_it != _thread_heads.end() && "Source thread must (still) exist");
			auto& source_event = source_it->second;
			assert(source_event->kind() != por::event::event_kind::thread_exit && "Source thread must not yet be exited");

			++_active_threads;
			assert(_active_threads > 0);
			source_event = event::thread_create::alloc(source, std::move(source_event));
			auto const tid = _next_thread++;
			assert(tid > 0);
			assert(thread_heads().find(tid) == thread_heads().end());
			_thread_heads.emplace(tid, event::thread_init::alloc(tid, source_event));
			return tid;
		}

		void exit_thread(event::thread_id_t thread) {
			auto thread_it = _thread_heads.find(thread);
			assert(thread_it != _thread_heads.end() && "Thread must (still) exist");
			auto& thread_event = thread_it->second;
			assert(thread_event->kind() != por::event::event_kind::thread_exit && "Thread must not yet be exited");

			assert(_active_threads > 0);
			--_active_threads;

			thread_event = event::thread_exit::alloc(thread, std::move(thread_event));
		}

		por::event::lock_id_t create_lock(event::thread_id_t thread) {
			auto thread_it = _thread_heads.find(thread);
			assert(thread_it != _thread_heads.end() && "Thread must (still) exist");
			auto& thread_event = thread_it->second;
			assert(thread_event->kind() != por::event::event_kind::thread_exit && "Thread must not yet be exited");

			auto const lock_id = _next_lock++;
			assert(lock_id > 0);
			assert(_lock_heads.find(lock_id) == _lock_heads.end());

			thread_event = event::lock_create::alloc(thread, std::move(thread_event));
			_lock_heads.emplace(lock_id, thread_event);
			return lock_id;
		}

		void destroy_lock(event::thread_id_t thread, event::lock_id_t lock) {
			auto thread_it = _thread_heads.find(thread);
			assert(thread_it != _thread_heads.end() && "Thread must (still) exist");
			auto& thread_event = thread_it->second;
			assert(thread_event->kind() != por::event::event_kind::thread_exit && "Thread must not yet be exited");
			auto lock_it = _lock_heads.find(lock);
			assert(_lock_heads.find(lock) != _lock_heads.end() && "Lock must (still) exist");

			thread_event = event::lock_destroy::alloc(thread, std::move(thread_event), std::move(lock_it->second));
			_lock_heads.erase(lock_it);
		}

		void acquire_lock(event::thread_id_t thread, event::lock_id_t lock) {
			auto thread_it = _thread_heads.find(thread);
			assert(thread_it != _thread_heads.end() && "Thread must (still) exist");
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
			assert(thread_it != _thread_heads.end() && "Thread must (still) exist");
			auto& thread_event = thread_it->second;
			assert(thread_event->kind() != por::event::event_kind::thread_exit && "Thread must not yet be exited");
			auto lock_it = _lock_heads.find(lock);
			assert(_lock_heads.find(lock) != _lock_heads.end() && "Lock must (still) exist");

			auto& lock_event = lock_it->second;
			thread_event = event::lock_release::alloc(thread, std::move(thread_event), std::move(lock_event));
			lock_event = thread_event;
		}

		void local(event::thread_id_t thread) {
			auto thread_it = _thread_heads.find(thread);
			assert(thread_it != _thread_heads.end() && "Thread must (still) exist");
			auto& thread_event = thread_it->second;
			assert(thread_event->kind() != por::event::event_kind::thread_exit && "Thread must not yet be exited");

			thread_event = event::local::alloc(thread, std::move(thread_event));
		}

	private:
		std::shared_ptr<por::event::event> const& get_thread_predecessor(std::shared_ptr<por::event::event> const& event) {
			switch(event->kind()) {
				case por::event::event_kind::broadcast:
					return static_cast<por::event::broadcast const*>(event.get())->thread_predecessor();
				case por::event::event_kind::condition_variable_create:
					return static_cast<por::event::condition_variable_create const*>(event.get())->thread_predecessor();
				case por::event::event_kind::condition_variable_destroy:
					return static_cast<por::event::condition_variable_destroy const*>(event.get())->thread_predecessor();
				case por::event::event_kind::local:
					return static_cast<por::event::local const*>(event.get())->thread_predecessor();
				case por::event::event_kind::lock_acquire:
					return static_cast<por::event::lock_acquire const*>(event.get())->thread_predecessor();
				case por::event::event_kind::lock_create:
					return static_cast<por::event::lock_create const*>(event.get())->thread_predecessor();
				case por::event::event_kind::lock_destroy:
					return static_cast<por::event::lock_destroy const*>(event.get())->thread_predecessor();
				case por::event::event_kind::lock_release:
					return static_cast<por::event::lock_release const*>(event.get())->thread_predecessor();
				case por::event::event_kind::signal:
					return static_cast<por::event::signal const*>(event.get())->thread_predecessor();
				case por::event::event_kind::thread_create:
					return static_cast<por::event::thread_create const*>(event.get())->thread_predecessor();
				case por::event::event_kind::thread_exit:
					return static_cast<por::event::thread_exit const*>(event.get())->thread_predecessor();
				case por::event::event_kind::thread_init:
					return event;
				case por::event::event_kind::thread_join:
					return static_cast<por::event::thread_join const*>(event.get())->thread_predecessor();
					return event;
				case por::event::event_kind::wait1:
					return static_cast<por::event::wait1 const*>(event.get())->thread_predecessor();
				case por::event::event_kind::wait2:
					return static_cast<por::event::wait2 const*>(event.get())->thread_predecessor();

				default:
					assert(0 && "event has no thread_predecessor");
			}
		}

		std::shared_ptr<por::event::event> const& get_lock_predecessor(std::shared_ptr<por::event::event> const& event) {
			switch(event->kind()) {
				case por::event::event_kind::lock_acquire:
					return static_cast<por::event::lock_acquire const*>(event.get())->lock_predecessor();
				case por::event::event_kind::lock_destroy:
					return static_cast<por::event::lock_destroy const*>(event.get())->lock_predecessor();
				case por::event::event_kind::lock_release:
					return static_cast<por::event::lock_release const*>(event.get())->lock_predecessor();
				case por::event::event_kind::wait1:
					return static_cast<por::event::wait1 const*>(event.get())->lock_predecessor();
				case por::event::event_kind::wait2:
					return static_cast<por::event::wait2 const*>(event.get())->lock_predecessor();
				default:
					assert(0 && "event has no lock_predecessor");
			}
		}

		std::vector<std::shared_ptr<por::event::event>> cex_acquire(std::shared_ptr<por::event::event> const& e) {
			std::vector<std::shared_ptr<por::event::event>> result;

			// immediate causal predecessor on same thread
			std::shared_ptr<por::event::event> const* et = nullptr;
			// maximal event concerning same lock in history of e
			std::shared_ptr<por::event::event> const* er = nullptr;
			// maximal event concerning same lock in [et]
			std::shared_ptr<por::event::event> const* em = nullptr;

			switch(e->kind()) {
				case por::event::event_kind::lock_acquire: {
					auto const* acq = static_cast<por::event::lock_acquire const*>(e.get());
					et = &acq->thread_predecessor();
					er = &acq->lock_predecessor();

					assert(et != nullptr && *et != nullptr);
					assert(er != nullptr && *er != nullptr);

					// er is on same thread (incl. er == et)
					if((*er)->tid() == e->tid()) {
						// this implies em == er (without the need to compute em)
						return {};
					}

					em = er;
					while(em != nullptr && *em != nullptr && *et < *em) {
						// descend chain of lock events until we are in [et]
						em = &get_lock_predecessor(*em);
					}

					if(em == nullptr || *em == nullptr ) {
						result.emplace_back(por::event::lock_acquire::alloc(e->tid(), *et, nullptr));
					}

					std::shared_ptr<por::event::event> const* ep = &get_lock_predecessor(*er);
					do {
						assert(ep != nullptr && *ep != nullptr);
						if((*ep)->kind() == por::event::event_kind::lock_release || (*ep)->kind() == por::event::event_kind::wait1) {
							result.emplace_back(por::event::lock_acquire::alloc(e->tid(), *et, *ep));
						}
						ep = &get_lock_predecessor(*ep);
					} while(*em < *ep || em == ep);

					return result;
				}
				case por::event::event_kind::wait2: {
					auto* w2 = static_cast<por::event::wait2 const*>(e.get());
					std::shared_ptr<por::event::event> const& et = w2->thread_predecessor();
					// TODO: find sig or broadcast
					//por::event::event* es = w2->condition_variable_predecessors()
					std::shared_ptr<por::event::event> const& er = w2->lock_predecessor();
					break;
				}
				default:
					assert(0);
			}
			return {};
		}

	public:
		std::vector<std::shared_ptr<por::event::event>> conflicting_extensions() {
			std::vector<std::shared_ptr<por::event::event>> S;
			for(auto& t : _thread_heads) {
				std::shared_ptr<por::event::event> const* e = &t.second;
				std::cerr << "cex starting with thread " << t.first << "\n";
				do {
					std::cerr << "cex for event " << (*e)->tid() << "@" << (*e)->depth() << "\n";
					switch((*e)->kind()) {
						case por::event::event_kind::lock_acquire:
						case por::event::event_kind::wait2:
							auto r = cex_acquire(*e);
							S.insert(S.end(), r.begin(), r.end());
							break;
					}
					bool has_predecessor = false;
					auto& p = get_thread_predecessor(*e);
					if(&p != e && p->tid() == t.first) {
						// in lieu of a more sophisticated set of visited events, stop when entering another thread
						e = &p;
						has_predecessor = true;
					}
					if(!has_predecessor) e = nullptr;
				} while(e != nullptr);
			}
			return S;
		}
	};

	inline configuration configuration_builder::construct() { return configuration(std::move(*this)); }
}
