#pragma once

#include "event/event.h"

#include <cassert>
#include <vector>
#include <map>

namespace por {
	class program {
		std::vector<std::shared_ptr<event::event>> _thread_heads;
		por::event::thread_id_t _active_threads = 0;

		std::map<event::lock_id_t, std::shared_ptr<event::event>> _lock_heads;
		event::lock_id_t _next_lock = 1;

	public:
		program() {
			_thread_heads.reserve(32);
			_thread_heads.emplace_back(event::program_start::alloc());
		}

		auto const& thread_heads() const noexcept { return _thread_heads; }
		auto const& lock_heads() const noexcept { return _lock_heads; }

		por::event::thread_id_t const& active_threads() const noexcept { return _active_threads; }

		// Spawn a new thread from tid `source`.
		// Use `source == 0` to indicate that the thread is spawned as part of program startup.
		por::event::thread_id_t spawn_thread(event::thread_id_t source) {
			assert(source < _thread_heads.size());
			assert((source > 0 || _thread_heads.size() == 1) && "In our current use cases there is always exactly one main thread spawned at program startup");
			assert(thread_heads()[source]->kind() != por::event::event_kind::thread_stop);

			++_active_threads;
			if(source > 0) {
				_thread_heads[source] = event::thread_create::alloc(source, _thread_heads[source]);
			}
			por::event::thread_id_t tid = static_cast<por::event::thread_id_t>(_thread_heads.size());
			assert(tid > 0);
			_thread_heads.emplace_back(event::thread_start::alloc(tid, _thread_heads[source]));
			return tid;
		}

		void stop_thread(event::thread_id_t thread) {
			assert(thread > 0);
			assert(thread < _thread_heads.size());
			assert(thread_heads()[thread]->kind() != por::event::event_kind::thread_stop);

			assert(_active_threads > 0);
			--_active_threads;

			_thread_heads[thread] = event::thread_stop::alloc(thread, _thread_heads[thread]);
		}

		por::event::lock_id_t create_lock(event::thread_id_t thread) {
			assert(thread > 0);
			assert(thread < _thread_heads.size());
			assert(thread_heads()[thread]->kind() != por::event::event_kind::thread_stop);

			auto const lock_id = _next_lock++;
			_thread_heads[thread] = event::lock_create::alloc(thread, _thread_heads[thread]);
			_lock_heads[lock_id] = _thread_heads[thread];
			return lock_id;
		}

		void destroy_lock(event::thread_id_t thread, event::lock_id_t lock) {
			assert(thread > 0);
			assert(thread < _thread_heads.size());
			assert(thread_heads()[thread]->kind() != por::event::event_kind::thread_stop);
			assert(lock_heads().find(lock) != lock_heads().end());

			_thread_heads[thread] = event::lock_destroy::alloc(thread, _thread_heads[thread], _lock_heads[lock]);
			_lock_heads[lock] = _thread_heads[thread];
		}

		void acquire_lock(event::thread_id_t thread, event::lock_id_t lock) {
			assert(thread > 0);
			assert(thread < _thread_heads.size());
			assert(thread_heads()[thread]->kind() != por::event::event_kind::thread_stop);
			assert(lock_heads().find(lock) != lock_heads().end());

			_thread_heads[thread] = event::lock_acquire::alloc(thread, _thread_heads[thread], _lock_heads[lock]);
			_lock_heads[lock] = _thread_heads[thread];
		}

		void release_lock(event::thread_id_t thread, event::lock_id_t lock) {
			assert(thread > 0);
			assert(thread < _thread_heads.size());
			assert(thread_heads()[thread]->kind() != por::event::event_kind::thread_stop);
			assert(lock_heads().find(lock) != lock_heads().end());

			_thread_heads[thread] = event::lock_release::alloc(thread, _thread_heads[thread], _lock_heads[lock]);
			_lock_heads[lock] = _thread_heads[thread];
		}

		void local(event::thread_id_t thread) {
			assert(thread > 0);
			assert(thread < _thread_heads.size());
			assert(thread_heads()[thread]->kind() != por::event::event_kind::thread_stop);

			_thread_heads[thread] = event::local::alloc(thread, _thread_heads[thread]);
		}
	};
}
