#pragma once

#include "base.h"

#include "por/unfolding.h"

#include <cassert>
#include <array>

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

			if(this->is_atomic()) {
				assert(this->thread_predecessor()->kind() == event_kind::lock_release);
			}
		}

	public:
		static event const& alloc(
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
		, _predecessors(std::move(that._predecessors))
		, _atomic(that._atomic) {
			assert(_predecessors.size() == 1);
			assert(thread_predecessor() != nullptr);
			replace_successor_of(*thread_predecessor(), that);
		}

		~thread_exit() {
			assert(!has_successors());
			assert(_predecessors.size() == 1);
			assert(thread_predecessor() != nullptr);
			remove_from_successors_of(*thread_predecessor());
		}

		thread_exit() = delete;
		thread_exit(const thread_exit&) = delete;
		thread_exit& operator=(const thread_exit&) = delete;
		thread_exit& operator=(thread_exit&&) = delete;

		std::string to_string(bool details) const noexcept override {
			if(details)
				return "[tid: " + tid().to_string() + " depth: " + std::to_string(depth()) + " kind: thread_exit"
					+ (is_atomic() ? " (atomic)" : "") + "]";
			return "thread_exit";
		}

		util::iterator_range<event const* const*> predecessors() const noexcept override {
			return util::make_iterator_range<event const* const*>(_predecessors.data(), _predecessors.data() + _predecessors.size());
		}

		event const* thread_predecessor() const noexcept override {
			return _predecessors[0];
		}

		bool is_atomic() const noexcept { return _atomic; }
	};
}
