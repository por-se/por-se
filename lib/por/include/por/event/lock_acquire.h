#pragma once

#include "base.h"

#include "por/unfolding.h"

#include <cassert>
#include <array>

namespace por::event {
	class lock_acquire final : public event {
		// predecessors:
		// 1. same-thread predecessor
		// 2. previous operation on same lock (may be nullptr if only preceded by lock_create event)
		std::array<event const*, 2> _predecessors;

		exploration_info _info;

	protected:
		lock_acquire(thread_id_t tid, event const& thread_predecessor, event const* lock_predecessor)
			: event(event_kind::lock_acquire, tid, thread_predecessor, lock_predecessor)
			, _predecessors{&thread_predecessor, lock_predecessor}
		{
			assert(this->thread_predecessor());
			assert(this->thread_predecessor()->tid());
			assert(this->thread_predecessor()->tid() == this->tid());
			assert(this->thread_predecessor()->kind() != event_kind::program_init);
			assert(this->thread_predecessor()->kind() != event_kind::thread_exit);

			if(this->lock_predecessor()) {
				assert(
					this->lock_predecessor()->kind() == event_kind::lock_create
					|| this->lock_predecessor()->kind() == event_kind::lock_release
					|| this->lock_predecessor()->kind() == event_kind::wait1
				);
			}
		}

	public:
		static event const& alloc(
			unfolding& unfolding,
			thread_id_t tid,
			event const& thread_predecessor,
			event const* lock_predecessor
		) {
			return unfolding.deduplicate(lock_acquire{
				tid,
				thread_predecessor,
				lock_predecessor
			});
		}

		void mark_as_open(path_t const& path) const override {
			_info.mark_as_open(path);
		}
		void mark_as_explored(path_t const& path) const override {
			_info.mark_as_explored(path);
		}
		bool is_present(path_t const& path) const override {
			return _info.is_present(path);
		}
		bool is_explored(path_t const& path) const override {
			return _info.is_explored(path);
		}

		std::string to_string(bool details) const override {
			if(details)
				return "[tid: " + tid().to_string() + " depth: " + std::to_string(depth()) + " kind: lock_acquire]";
			return "lock_acquire";
		}

		util::iterator_range<event const* const*> predecessors() const noexcept override {
			if(_predecessors[1] != nullptr) {
				return util::make_iterator_range<event const* const*>(_predecessors.data(), _predecessors.data() + 2);
			} else {
				return util::make_iterator_range<event const* const*>(_predecessors.data(), _predecessors.data() + 1);
			}
		}

		event const* thread_predecessor() const override {
			return _predecessors[0];
		}

		// may return nullptr if only preceded by lock_create event
		event const* lock_predecessor() const noexcept override { return _predecessors[1]; }
	};
}
