#pragma once

#include "base.h"

#include "por/unfolding.h"

#include <cassert>
#include <array>

namespace por::event {
	class thread_join final : public event {
		// predecessors:
		// 1. same-thread predecessor
		// 2. joined predecessor
		std::array<event const*, 2> _predecessors;

	protected:
		thread_join(thread_id_t tid, event const& thread_predecessor, event const& joined_predecessor)
			: event(event_kind::thread_join, tid, thread_predecessor, &joined_predecessor)
			, _predecessors{&thread_predecessor, &joined_predecessor}
		{
			assert(this->thread_predecessor());
			assert(this->thread_predecessor()->tid());
			assert(this->thread_predecessor()->tid() == this->tid());
			assert(this->thread_predecessor()->kind() != event_kind::program_init);
			assert(this->thread_predecessor()->kind() != event_kind::thread_exit);
			assert(this->joined_thread());
			assert(this->joined_thread()->tid());
			assert(this->joined_thread()->tid() != this->tid());
			assert(this->joined_thread()->kind() == event_kind::thread_exit);
		}

	public:
		static event const& alloc(
			unfolding& unfolding,
			thread_id_t tid,
			event const& thread_predecessor,
			event const& joined_predecessor
		) {
			return unfolding.deduplicate(thread_join{
				tid,
				thread_predecessor,
				joined_predecessor
			});
		}

		std::string to_string(bool details) const override {
			if(details)
				return "[tid: " + tid().to_string() + " depth: " + std::to_string(depth()) + " kind: thread_join with: " + joined_thread()->tid().to_string() + "]";
			return "thread_join";
		}

		util::iterator_range<event const* const*> predecessors() const override {
			return util::make_iterator_range<event const* const*>(_predecessors.data(), _predecessors.data() + _predecessors.size());
		}

		event const* thread_predecessor() const override {
			return _predecessors[0];
		}

		event const* joined_thread() const noexcept { return _predecessors[1]; }
	};
}
