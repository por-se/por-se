#pragma once

#include "base.h"

#include "por/unfolding.h"

#include <array>
#include <cassert>

namespace por::event {
	class thread_exit final : public event {
		// predecessors:
		// 1. same-thread predecessor
		std::array<event const*, 1> _predecessors;

		bool _atomic;

	protected:
		thread_exit(thread_id_t tid, event const& thread_predecessor, bool atomic)
			: event(event_kind::thread_exit, tid, thread_predecessor)
			, _predecessors{&thread_predecessor}
			, _atomic(atomic)
		{
			assert(this->thread_predecessor());
			assert(this->thread_predecessor()->tid());
			assert(this->thread_predecessor()->tid() == this->tid());
			assert(this->thread_predecessor()->kind() != event_kind::program_init);
			assert(this->thread_predecessor()->kind() != event_kind::thread_exit);

			if(this->ends_atomic_operation()) {
				assert(this->atomic_predecessor());
				assert(this->atomic_predecessor() == this->thread_predecessor());
				assert(this->atomic_predecessor()->kind() == event_kind::lock_release);
			} else {
				assert(this->atomic_predecessor() == nullptr);
			}
		}

	public:
		static por::unfolding::deduplication_result alloc(
			unfolding& unfolding,
			thread_id_t tid,
			event const& thread_predecessor,
			bool atomic = false
		) {
			return unfolding.deduplicate(thread_exit{
				tid,
				thread_predecessor,
				atomic
			});
		}

		thread_exit(thread_exit&& that)
		: event(std::move(that))
		, _predecessors(that._predecessors)
		, _atomic(that._atomic) {
			that._predecessors = {};
			assert(_predecessors.size() == 1);
			assert(thread_predecessor() != nullptr);
		}

		~thread_exit() {
			assert(!has_successors());
			if(thread_predecessor() != nullptr) {
				remove_from_successors_of(*thread_predecessor());
			}
		}

		thread_exit() = delete;
		thread_exit(const thread_exit&) = delete;
		thread_exit& operator=(const thread_exit&) = delete;
		thread_exit& operator=(thread_exit&&) = delete;

		std::string to_string(bool details) const noexcept override {
			if(details)
				return "[tid: " + tid().to_string() + " depth: " + std::to_string(depth()) + " kind: thread_exit"
					+ (ends_atomic_operation() ? " (atomic)" : "") + (is_cutoff() ? " CUTOFF" : "") + "]";
			return "thread_exit";
		}

		util::iterator_range<event const* const*> predecessors() const noexcept override {
			if(_predecessors[0] == nullptr) {
				return util::make_iterator_range<event const* const*>(nullptr, nullptr); // only after move-ctor
			}
			return util::make_iterator_range<event const* const*>(_predecessors.data(), _predecessors.data() + _predecessors.size());
		}

		immediate_predecessor_range_t immediate_predecessors() const noexcept override {
			return make_immediate_predecessor_range(predecessors());
		}

		event const* thread_predecessor() const noexcept override {
			return _predecessors[0];
		}

		bool ends_atomic_operation() const noexcept override { return _atomic; }

		event const* atomic_predecessor() const noexcept override {
			if(ends_atomic_operation()) {
				return thread_predecessor();
			}
			return nullptr;
		}
	};
}
