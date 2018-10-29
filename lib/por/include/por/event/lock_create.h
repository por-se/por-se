#pragma once

#include "base.h"

#include <cassert>

namespace por::event {
	class lock_create final : public event {
		// predecessors:
		// 1. same-thread predecessor
		std::array<std::shared_ptr<event>, 1> _predecessors;

	protected:
		lock_create(thread_id_t tid, std::shared_ptr<event> const& thread_predecessor)
		: event(event_kind::lock_create, tid)
		, _predecessors{thread_predecessor}
		{
			assert(thread_dependency());
			assert(thread_dependency()->tid() != 0);
			assert(thread_dependency()->tid() == this->tid());
			assert(thread_dependency()->kind() != event_kind::program_start);
			assert(thread_dependency()->kind() != event_kind::thread_stop);
		}

	public:
		static std::shared_ptr<lock_create> alloc(thread_id_t tid, std::shared_ptr<event> const& thread_predecessor) {
			return std::make_shared<lock_create>(lock_create{tid, thread_predecessor});
		}

		virtual util::iterator_range<std::shared_ptr<event>*> predecessors() override {
			return util::make_iterator_range<std::shared_ptr<event>*>(_predecessors.data(), _predecessors.data() + _predecessors.size());
		}

		std::shared_ptr<event>      & thread_dependency()       noexcept { return _predecessors[0]; }
		std::shared_ptr<event> const& thread_dependency() const noexcept { return _predecessors[0]; }
	};
}
