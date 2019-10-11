#pragma once

#include "base.h"

#include "por/unfolding.h"

#include <cassert>
#include <array>

namespace por::event {
	class thread_create final : public event {
		// predecessors:
		// 1. same-thread predecessor
		std::array<event const*, 1> _predecessors;

	protected:
		thread_create(thread_id_t tid, event const& thread_predecessor)
			: event(event_kind::thread_create, tid, thread_predecessor)
			, _predecessors{&thread_predecessor}
		{
			assert(this->thread_predecessor());
			assert(this->thread_predecessor()->tid());
			assert(this->thread_predecessor()->tid() == this->tid());
			assert(this->thread_predecessor()->kind() != event_kind::program_init);
			assert(this->thread_predecessor()->kind() != event_kind::thread_exit);
		}

	public:
		static event const& alloc(
			unfolding& unfolding,
			thread_id_t tid,
			event const& thread_predecessor
		) {
			return unfolding.deduplicate(thread_create{
				tid,
				thread_predecessor
			});
		}

		std::string to_string(bool details) const override {
			if(details)
				return "[tid: " + tid().to_string() + " depth: " + std::to_string(depth()) + " kind: thread_create]";
			return "thread_create";
		}

		util::iterator_range<event const* const*> predecessors() const noexcept override {
			return util::make_iterator_range<event const* const*>(_predecessors.data(), _predecessors.data() + _predecessors.size());
		}

		event const* thread_predecessor() const override {
			return _predecessors[0];
		}
	};
}
