#pragma once

#include <util/iterator_range.h>

#include <cstdint>
#include <array>
#include <memory>

namespace por::event {
	using thread_id_t = std::uint32_t;
	using lock_id_t = std::uint64_t;

	enum class event_kind : std::uint8_t {
		local = 0,
		program_init = 1,
		thread_create = 2,
		thread_init = 3,
		thread_exit = 4,
		lock_create = 5,
		lock_destroy = 6,
		lock_acquire = 7,
		lock_release = 8,
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
