#pragma once

#include "base.h"

#include <cassert>
#include <array>
#include <memory>

namespace por::event {
	class lock_destroy final : public event {
		// predecessors:
		// 1. same-thread predecessor
		// 2. previous operation on same lock (may be nullptr if only preceded by lock_create event)
		std::array<std::shared_ptr<event>, 2> _predecessors;

	protected:
		lock_destroy(thread_id_t tid, std::shared_ptr<event>&& thread_predecessor, std::shared_ptr<event>&& lock_predecessor)
			: event(event_kind::lock_destroy, tid, thread_predecessor, lock_predecessor)
			, _predecessors{std::move(thread_predecessor), std::move(lock_predecessor)}
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
		static std::shared_ptr<event> alloc(
			std::shared_ptr<unfolding>& unfolding,
			thread_id_t tid,
			std::shared_ptr<event> thread_predecessor,
			std::shared_ptr<event> lock_predecessor
		) {
			return deduplicate(unfolding, std::make_shared<lock_destroy>(
				lock_destroy{
					tid,
					std::move(thread_predecessor),
					std::move(lock_predecessor)
				}
			));
		}

		virtual std::string to_string(bool details) const override {
			if(details)
				return "[tid: " + tid().to_string() + " depth: " + std::to_string(depth()) + " kind: lock_destroy]";
			return "lock_destroy";
		}

		virtual util::iterator_range<std::shared_ptr<event>*> predecessors() override {
			if(_predecessors[1] != nullptr) {
				return util::make_iterator_range<std::shared_ptr<event>*>(_predecessors.data(), _predecessors.data() + 2);
			} else {
				return util::make_iterator_range<std::shared_ptr<event>*>(_predecessors.data(), _predecessors.data() + 1);
			}
		}

		virtual util::iterator_range<std::shared_ptr<event> const*> predecessors() const override {
			if(_predecessors[1] != nullptr) {
				return util::make_iterator_range<std::shared_ptr<event> const*>(_predecessors.data(), _predecessors.data() + 2);
			} else {
				return util::make_iterator_range<std::shared_ptr<event> const*>(_predecessors.data(), _predecessors.data() + 1);
			}
		}

		std::shared_ptr<event>      & thread_predecessor()       noexcept { return _predecessors[0]; }
		std::shared_ptr<event> const& thread_predecessor() const noexcept { return _predecessors[0]; }

		// may return nullptr if only preceded by lock_create event
		std::shared_ptr<event>      & lock_predecessor()       noexcept { return _predecessors[1]; }
		// may return nullptr if only preceded by lock_create event
		std::shared_ptr<event> const& lock_predecessor() const noexcept { return _predecessors[1]; }
	};
}
