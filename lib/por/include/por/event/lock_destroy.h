#pragma once

#include "base.h"

#include <cassert>
#include <array>

namespace por::event {
	class lock_destroy final : public event {
		// predecessors:
		// 1. same-thread predecessor
		// 2. previous operation on same lock (may be nullptr if only preceded by lock_create event)
		std::array<event const*, 2> _predecessors;

	protected:
		lock_destroy(thread_id_t tid, event const& thread_predecessor, event const* lock_predecessor)
			: event(event_kind::lock_destroy, tid, thread_predecessor, lock_predecessor)
			, _predecessors{&thread_predecessor, lock_predecessor}
		{
			assert(this->thread_predecessor());
			assert(this->thread_predecessor()->tid());
			assert(this->thread_predecessor()->tid() == this->tid());
			assert(this->thread_predecessor()->kind() != event_kind::program_init);
			assert(this->thread_predecessor()->kind() != event_kind::thread_exit);

			if(this->lock_predecessor()) {
				assert(this->lock_predecessor()->kind() != event_kind::lock_acquire && "destroying an acquired lock is UB");
				assert(this->lock_predecessor()->kind() == event_kind::lock_create || this->lock_predecessor()->kind() == event_kind::lock_release);
			}
		}

	public:
		static event const& alloc(
			unfolding& unfolding,
			thread_id_t tid,
			event const& thread_predecessor,
			event const* lock_predecessor
		) {
			return deduplicate(unfolding, lock_destroy(
				tid,
				thread_predecessor,
				lock_predecessor
			));
		}

		virtual std::string to_string(bool details) const override {
			if(details)
				return "[tid: " + tid().to_string() + " depth: " + std::to_string(depth()) + " kind: lock_destroy]";
			return "lock_destroy";
		}

		virtual util::iterator_range<event const* const*> predecessors() const override {
			if(_predecessors[1] != nullptr) {
				return util::make_iterator_range<event const* const*>(_predecessors.data(), _predecessors.data() + 2);
			} else {
				return util::make_iterator_range<event const* const*>(_predecessors.data(), _predecessors.data() + 1);
			}
		}

		virtual event const* thread_predecessor() const override {
			return _predecessors[0];
		}

		// may return nullptr if only preceded by lock_create event
		event const* lock_predecessor() const noexcept { return _predecessors[1]; }
	};
}
