#pragma once

#include "base.h"

#include "por/unfolding.h"

#include <cassert>
#include <array>

namespace por::event {
	class lock_release final : public event {
		// predecessors:
		// 1. same-thread predecessor
		// 2. previous acquisition of this lock
		std::array<event const*, 2> _predecessors;

	protected:
		lock_release(thread_id_t tid, event const& thread_predecessor, event const& lock_predecessor)
			: event(event_kind::lock_release, tid, thread_predecessor, &lock_predecessor)
			, _predecessors{&thread_predecessor, &lock_predecessor}
		{
			assert(this->thread_predecessor());
			assert(this->thread_predecessor()->tid());
			assert(this->thread_predecessor()->tid() == this->tid());
			assert(this->thread_predecessor()->kind() != event_kind::program_init);
			assert(this->thread_predecessor()->kind() != event_kind::thread_exit);
			assert(this->lock_predecessor());
			assert(
				this->lock_predecessor()->kind() == event_kind::lock_acquire
				|| this->lock_predecessor()->kind() == event_kind::wait2
			);
			assert(this->lock_predecessor()->tid() == this->tid());
		}

	public:
		static event const& alloc(
			unfolding& unfolding,
			thread_id_t tid,
			event const& thread_predecessor,
			event const& lock_predecessor
		) {
			return unfolding.deduplicate(lock_release{
				tid,
				thread_predecessor,
				lock_predecessor
			});
		}

		lock_release(lock_release&& that)
		: event(std::move(that))
		, _predecessors(std::move(that._predecessors)) {
			for(auto& pred : predecessors()) {
				assert(pred != nullptr);
				replace_successor_of(*pred, that);
			}
		}

		~lock_release() {
			assert(!has_successors());
			for(auto& pred : predecessors()) {
				assert(pred != nullptr);
				remove_from_successors_of(*pred);
			}
		}

		lock_release() = delete;
		lock_release(const lock_release&) = delete;
		lock_release& operator=(const lock_release&) = delete;
		lock_release& operator=(lock_release&&) = delete;

		std::string to_string(bool details) const override {
			if(details)
				return "[tid: " + tid().to_string() + " depth: " + std::to_string(depth()) + " kind: lock_release]";
			return "lock_release";
		}

		util::iterator_range<event const* const*> predecessors() const noexcept override {
			return util::make_iterator_range<event const* const*>(_predecessors.data(), _predecessors.data() + _predecessors.size());
		}

		event const* thread_predecessor() const override {
			return _predecessors[0];
		}

		event const* lock_predecessor() const noexcept override { return _predecessors[1]; }
	};
}
