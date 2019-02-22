#pragma once

#include "base.h"

#include <cassert>
#include <array>
#include <memory>

namespace por::event {
	class lock_release final : public event {
		// predecessors:
		// 1. same-thread predecessor
		// 2. previous acquisition of this lock
		std::array<std::shared_ptr<event>, 2> _predecessors;

	protected:
		lock_release(thread_id_t tid, std::shared_ptr<event>&& thread_predecessor, std::shared_ptr<event>&& lock_predecessor)
			: event(event_kind::lock_release, tid, thread_predecessor, lock_predecessor)
			, _predecessors{std::move(thread_predecessor), std::move(lock_predecessor)}
		{
			assert(this->thread_predecessor());
			assert(this->thread_predecessor()->tid() != 0);
			assert(this->thread_predecessor()->tid() == this->tid());
			assert(this->thread_predecessor()->kind() != event_kind::program_init);
			assert(this->thread_predecessor()->kind() != event_kind::thread_exit);
			assert(this->lock_predecessor());
			assert(
				this->lock_predecessor()->kind() == event_kind::lock_acquire
				&& this->lock_predecessor()->tid() == this->tid()
			);
		}

	public:
		static std::shared_ptr<lock_release> alloc(thread_id_t tid, std::shared_ptr<event> thread_predecessor, std::shared_ptr<event> lock_predecessor) {
			return std::make_shared<lock_release>(lock_release{tid, std::move(thread_predecessor), std::move(lock_predecessor)});
		}

		virtual util::iterator_range<std::shared_ptr<event>*> predecessors() override {
			return util::make_iterator_range<std::shared_ptr<event>*>(_predecessors.data(), _predecessors.data() + _predecessors.size());
		}

		virtual util::iterator_range<std::shared_ptr<event> const*> predecessors() const override {
			return util::make_iterator_range<std::shared_ptr<event> const*>(_predecessors.data(), _predecessors.data() + _predecessors.size());
		}

		std::shared_ptr<event>      & thread_predecessor()       noexcept { return _predecessors[0]; }
		std::shared_ptr<event> const& thread_predecessor() const noexcept { return _predecessors[0]; }

		std::shared_ptr<event>      & lock_predecessor()       noexcept { return _predecessors[1]; }
		std::shared_ptr<event> const& lock_predecessor() const noexcept { return _predecessors[1]; }
	};
}
