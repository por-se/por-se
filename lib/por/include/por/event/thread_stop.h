#pragma once

#include "base.h"

namespace por::event {
	class thread_stop final : public event {
		// predecessors:
		// 1. thread creation predecessor (must be a different thread or program stop)
		std::array<std::shared_ptr<event>, 1> _predecessors;

	protected:
		thread_stop(thread_id_t tid, std::shared_ptr<event>&& thread_predecessor)
		: event(event_kind::thread_stop, tid)
		, _predecessors{std::move(thread_predecessor)}
		{
			assert(this->thread_predecessor());
			assert(this->thread_predecessor()->tid() != 0);
			assert(this->thread_predecessor()->tid() == this->tid());
			assert(this->thread_predecessor()->kind() != event_kind::program_start);
			assert(this->thread_predecessor()->kind() != event_kind::thread_stop);
		}

	public:
		static std::shared_ptr<thread_stop> alloc(thread_id_t tid, std::shared_ptr<event> thread_predecessor) {
			return std::make_shared<thread_stop>(thread_stop{tid, std::move(thread_predecessor)});
		}

		virtual util::iterator_range<std::shared_ptr<event>*> predecessors() override {
			return util::make_iterator_range<std::shared_ptr<event>*>(_predecessors.data(), _predecessors.data() + _predecessors.size());
		}

		std::shared_ptr<event>      & thread_predecessor()       noexcept { return _predecessors[0]; }
		std::shared_ptr<event> const& thread_predecessor() const noexcept { return _predecessors[0]; }
	};
}
