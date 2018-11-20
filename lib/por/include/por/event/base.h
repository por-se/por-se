#pragma once

#include <util/iterator_range.h>

#include <cstdint>
#include <memory>

namespace por::event {
	using thread_id_t = std::uint32_t;
	using lock_id_t = std::uint64_t;

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

	class event : public std::enable_shared_from_this<event> {
		event_kind _kind;
		thread_id_t _tid;

	public:
		event_kind kind() const noexcept { return _kind; }
		thread_id_t tid() const noexcept { return _tid; }

	protected:
		event(event_kind kind, thread_id_t tid)
		: _kind(kind)
		, _tid(tid)
		{ }

	public:
		virtual util::iterator_range<std::shared_ptr<event>*> predecessors() = 0;
	};
}
