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

		thread_id_t _created_tid;

	protected:
		thread_create(thread_id_t tid, event const& thread_predecessor, thread_id_t new_tid)
			: event(event_kind::thread_create, tid, thread_predecessor)
			, _predecessors{&thread_predecessor}
			, _created_tid{new_tid}
		{
			assert(this->thread_predecessor());
			assert(this->thread_predecessor()->tid());
			assert(this->thread_predecessor()->tid() == this->tid());
			assert(this->thread_predecessor()->kind() != event_kind::program_init);
			assert(this->thread_predecessor()->kind() != event_kind::thread_exit);
			assert(this->created_tid());
			assert(this->created_tid() != this->tid());
		}

	public:
		static event const& alloc(
			unfolding& unfolding,
			thread_id_t tid,
			event const& thread_predecessor,
			thread_id_t new_tid
		) {
			return unfolding.deduplicate(thread_create{
				std::move(tid),
				thread_predecessor,
				std::move(new_tid)
			});
		}

		thread_create(thread_create&& that)
		: event(std::move(that))
		, _predecessors(std::move(that._predecessors))
		, _created_tid(std::move(that._created_tid)) {
			assert(_predecessors.size() == 1);
			assert(thread_predecessor() != nullptr);
			replace_successor_of(*thread_predecessor(), that);
		}

		~thread_create() {
			assert(!has_successors());
			assert(_predecessors.size() == 1);
			assert(thread_predecessor() != nullptr);
			remove_from_successors_of(*thread_predecessor());
		}

		thread_create() = delete;
		thread_create(const thread_create&) = delete;
		thread_create& operator=(const thread_create&) = delete;
		thread_create& operator=(thread_create&&) = delete;

		std::string to_string(bool details) const override {
			if(details)
				return "[tid: " + tid().to_string() + " depth: " + std::to_string(depth()) + " kind: thread_create created: " + created_tid().to_string() + "]";
			return "thread_create";
		}

		util::iterator_range<event const* const*> predecessors() const noexcept override {
			return util::make_iterator_range<event const* const*>(_predecessors.data(), _predecessors.data() + _predecessors.size());
		}

		event const* thread_predecessor() const override {
			return _predecessors[0];
		}

		thread_id_t created_tid() const noexcept {
			return _created_tid;
		}
	};
}
