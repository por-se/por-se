#pragma once

#include "base.h"

#include "por/unfolding.h"

#include <cassert>
#include <array>

namespace por::event {
	class lock_create final : public event {
		// predecessors:
		// 1. same-thread predecessor
		std::array<event const*, 1> _predecessors;

		lock_id_t _lid;

	protected:
		lock_create(thread_id_t tid, lock_id_t lid, event const& thread_predecessor)
			: event(event_kind::lock_create, tid, thread_predecessor)
			, _predecessors{&thread_predecessor}
			, _lid(lid)
		{
			assert(this->thread_predecessor());
			assert(this->thread_predecessor()->tid());
			assert(this->thread_predecessor()->tid() == this->tid());
			assert(this->thread_predecessor()->kind() != event_kind::program_init);
			assert(this->thread_predecessor()->kind() != event_kind::thread_exit);
			assert(this->lid());
		}

	public:
		static event const& alloc(
			unfolding& unfolding,
			thread_id_t tid,
			lock_id_t lid,
			event const& thread_predecessor
		) {
			return unfolding.deduplicate(lock_create{
				tid,
				lid,
				thread_predecessor
			});
		}

		lock_create(lock_create&& that)
		: event(std::move(that))
		, _predecessors(std::move(that._predecessors))
		, _lid(std::move(that._lid)) {
			assert(_predecessors.size() == 1);
			assert(thread_predecessor() != nullptr);
			replace_successor_of(*thread_predecessor(), that);
		}

		~lock_create() {
			assert(!has_successors());
			assert(_predecessors.size() == 1);
			assert(thread_predecessor() != nullptr);
			remove_from_successors_of(*thread_predecessor());
		}

		lock_create() = delete;
		lock_create(const lock_create&) = delete;
		lock_create& operator=(const lock_create&) = delete;
		lock_create& operator=(lock_create&&) = delete;

		std::string to_string(bool details) const override {
			if(details)
				return "[tid: " + tid().to_string() + " depth: " + std::to_string(depth()) + " kind: lock_create lid: " + std::to_string(lid()) + "]";
			return "lock_create";
		}

		util::iterator_range<event const* const*> predecessors() const noexcept override {
			return util::make_iterator_range<event const* const*>(_predecessors.data(), _predecessors.data() + _predecessors.size());
		}

		event const* thread_predecessor() const noexcept override {
			return _predecessors[0];
		}

		lock_id_t lid() const noexcept override { return _lid; }
	};
}
