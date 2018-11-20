#pragma once

#include "base.h"

#include <cassert>
#include <array>
#include <memory>

namespace por::event {
	class lock_acquire final : public event {
		// predecessors:
		// 1. same-thread predecessor
		// 2. previous release of this lock
		std::array<std::shared_ptr<event>, 2> _predecessors;

	protected:
		lock_acquire(thread_id_t tid, std::shared_ptr<event>&& thread_predecessor, std::shared_ptr<event>&& lock_predecessor)
			: event(event_kind::lock_acquire, tid)
			, _predecessors{std::move(thread_predecessor), std::move(lock_predecessor)}
		{
			assert(this->thread_predecessor());
			assert(this->thread_predecessor()->tid() != 0);
			assert(this->thread_predecessor()->tid() == this->tid());
			assert(this->thread_predecessor()->kind() != event_kind::program_init);
			assert(this->thread_predecessor()->kind() != event_kind::thread_exit);
			assert(this->lock_predecessor());
			assert(
				this->lock_predecessor()->kind() == event_kind::lock_create
				|| this->lock_predecessor()->kind() == event_kind::lock_release
			);
		}

	public:
		static std::shared_ptr<lock_acquire> alloc(thread_id_t tid, std::shared_ptr<event> thread_predecessor, std::shared_ptr<event> lock_predecessor) {
			return std::make_shared<lock_acquire>(lock_acquire{tid, std::move(thread_predecessor), std::move(lock_predecessor)});
		}

		virtual util::iterator_range<std::shared_ptr<event>*> predecessors() override {
			return util::make_iterator_range<std::shared_ptr<event>*>(_predecessors.data(), _predecessors.data() + _predecessors.size());
		}

		std::shared_ptr<event>      & thread_predecessor()       noexcept { return _predecessors[0]; }
		std::shared_ptr<event> const& thread_predecessor() const noexcept { return _predecessors[0]; }

		std::shared_ptr<event>      & lock_predecessor()       noexcept { return _predecessors[1]; }
		std::shared_ptr<event> const& lock_predecessor() const noexcept { return _predecessors[1]; }
	};
}
