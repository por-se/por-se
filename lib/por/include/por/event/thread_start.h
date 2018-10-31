#pragma once

#include "base.h"

namespace por::event {
	class thread_start final : public event {
		// predecessors:
		// 1. thread creation predecessor (must be a different thread or program start)
		std::array<std::shared_ptr<event>, 1> _predecessors;

	protected:
		thread_start(thread_id_t tid, std::shared_ptr<event>&& creator)
		: event(event_kind::thread_start, tid)
		, _predecessors{std::move(creator)}
		{
			assert(this->thread_creation_predecessor());
			assert(
				this->thread_creation_predecessor()->kind() == event_kind::program_start
				|| this->thread_creation_predecessor()->kind() == event_kind::thread_create
			);
		}

	public:
		static std::shared_ptr<thread_start> alloc(thread_id_t tid, std::shared_ptr<event> creator) {
			return std::make_shared<thread_start>(thread_start{tid, std::move(creator)});
		}

		virtual util::iterator_range<std::shared_ptr<event>*> predecessors() override {
			return util::make_iterator_range<std::shared_ptr<event>*>(_predecessors.data(), _predecessors.data() + _predecessors.size());
		}

		std::shared_ptr<event>      & thread_creation_predecessor()       noexcept { return _predecessors[0]; }
		std::shared_ptr<event> const& thread_creation_predecessor() const noexcept { return _predecessors[0]; }
	};
}
