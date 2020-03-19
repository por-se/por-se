#pragma once

#include <cstdint>

namespace por::event {
	enum class event_kind : std::uint8_t {
		local,
		program_init,
		thread_create,
		thread_join,
		thread_init,
		thread_exit,
		lock_create,
		lock_destroy,
		lock_acquire,
		lock_release,
		condition_variable_create,
		condition_variable_destroy,
		wait1,
		wait2,
		signal,
		broadcast,
	};

	template<typename OS>
	OS& operator<<(OS& os, por::event::event_kind kind) {
		using por::event::event_kind;

		switch(kind) {
			case event_kind::local: os << "local"; break;
			case event_kind::program_init: os << "program_init"; break;
			case event_kind::thread_create: os << "thread_create"; break;
			case event_kind::thread_join: os << "thread_join"; break;
			case event_kind::thread_init: os << "thread_init"; break;
			case event_kind::thread_exit: os << "thread_exit"; break;
			case event_kind::lock_create: os << "lock_create"; break;
			case event_kind::lock_destroy: os << "lock_destroy"; break;
			case event_kind::lock_acquire: os << "lock_acquire"; break;
			case event_kind::lock_release: os << "lock_release"; break;
			case event_kind::condition_variable_create: os << "condition_variable_create"; break;
			case event_kind::condition_variable_destroy: os << "condition_variable_destroy"; break;
			case event_kind::wait1: os << "wait1"; break;
			case event_kind::wait2: os << "wait2"; break;
			case event_kind::signal: os << "signal"; break;
			case event_kind::broadcast: os << "broadcast"; break;
		}

		return os;
	}
}
