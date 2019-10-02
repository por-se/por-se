#pragma once

#include "base.h"

#include "por/unfolding.h"

#include <cassert>
#include <array>

namespace por::event {
	class thread_init final : public event {
		// predecessors:
		// 1. thread creation predecessor (must be a different thread or program init)
		std::array<event const*, 1> _predecessors;

	protected:
		thread_init(thread_id_t tid, event const& creation_predecessor)
			: event(event_kind::thread_init, tid, creation_predecessor)
			, _predecessors{&creation_predecessor}
		{
			assert(this->thread_creation_predecessor());
			assert(this->thread_creation_predecessor()->tid() != this->tid());
			assert(
				(this->thread_creation_predecessor()->kind() == event_kind::program_init
					&& !this->thread_creation_predecessor()->tid())
				|| (this->thread_creation_predecessor()->kind() == event_kind::thread_create
					&& this->thread_creation_predecessor()->tid())
			);
		}

	public:
		static event const& alloc(
			unfolding& unfolding,
			thread_id_t tid,
			event const& creation_predecessor
		) {
			return unfolding.deduplicate(thread_init{
				tid,
				creation_predecessor
			});
		}

		virtual std::string to_string(bool details) const override {
			if(details)
				return "[tid: " + tid().to_string() + " depth: " + std::to_string(depth()) + " kind: thread_init]";
			return "thread_init";
		}

		virtual util::iterator_range<event const* const*> predecessors() const override {
			return util::make_iterator_range<event const* const*>(_predecessors.data(), _predecessors.data() + _predecessors.size());
		}

		virtual event const* thread_predecessor() const override {
			return nullptr;
		}

		event const* thread_creation_predecessor() const noexcept { return _predecessors[0]; }
	};
}
