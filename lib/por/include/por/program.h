#pragma once

#include "event/event.h"

#include <cassert>
#include <map>

namespace por {
	class program;

	class program_builder{
		friend class program;

		std::shared_ptr<por::event::program_init> _program_init = event::program_init::alloc();

		std::map<por::event::thread_id_t, std::shared_ptr<event::event>> _thread_heads;
		por::event::thread_id_t _next_thread = 1;

		std::map<event::lock_id_t, std::shared_ptr<event::event>> _lock_heads;
		por::event::lock_id_t _next_lock = 1;

	public:
		program construct();

		program_builder& add_thread() {
			auto const tid = _next_thread++;
			assert(tid > 0);

			_thread_heads.emplace(tid, event::thread_init::alloc(tid, _program_init));

			return *this;
		}
	};

	class program {
		std::map<por::event::thread_id_t, std::shared_ptr<event::event>> _thread_heads;
		por::event::thread_id_t _next_thread = 1;
		por::event::thread_id_t _active_threads = 0; // really optimizes for a case we will not care about outside of simulations

		std::map<event::lock_id_t, std::shared_ptr<event::event>> _lock_heads;
		por::event::lock_id_t _next_lock = 1;

	public:
		program() : program(program_builder{}.add_thread().construct()) { }
		program(program const&) = delete;
		program& operator=(program const&) = delete;
		program(program&&) = default;
		program& operator=(program&&) = default;
		program(program_builder&& builder)
			: _thread_heads(std::move(builder._thread_heads))
			, _next_thread(std::move(builder._next_thread))
			, _lock_heads(std::move(builder._lock_heads))
			, _next_lock(std::move(builder._next_lock))
		{
			assert(!_thread_heads.empty() && "Cannot create a program without any startup threads");
			assert(_next_thread > 0);
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
	};

	inline program program_builder::construct() { return program(std::move(*this)); }
}
