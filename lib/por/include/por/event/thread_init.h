#pragma once

#include "base.h"

#include <cassert>
#include <array>
#include <memory>

namespace por::event {
	class thread_init final : public event {
		// predecessors:
		// 1. thread creation predecessor (must be a different thread or program init)
		std::array<std::shared_ptr<event>, 1> _predecessors;

	protected:
		thread_init(thread_id_t tid, std::shared_ptr<event>&& creator)
			: event(event_kind::thread_init, tid, creator)
			, _predecessors{std::move(creator)}
		{
			assert(this->thread_creation_predecessor());
			assert(this->thread_creation_predecessor()->tid() != this->tid());
			assert(
				(this->thread_creation_predecessor()->kind() == event_kind::program_init
					&& this->thread_creation_predecessor()->tid() == 0)
				|| (this->thread_creation_predecessor()->kind() == event_kind::thread_create
					&& this->thread_creation_predecessor()->tid() > 0)
			);
		}

	public:
		static std::shared_ptr<thread_init> alloc(thread_id_t tid, std::shared_ptr<event> creator) {
			return std::make_shared<thread_init>(thread_init{tid, std::move(creator)});
		}

		virtual util::iterator_range<std::shared_ptr<event>*> predecessors() override {
			return util::make_iterator_range<std::shared_ptr<event>*>(_predecessors.data(), _predecessors.data() + _predecessors.size());
		}

		virtual util::iterator_range<std::shared_ptr<event> const*> predecessors() const override {
			return util::make_iterator_range<std::shared_ptr<event> const*>(_predecessors.data(), _predecessors.data() + _predecessors.size());
		}

		std::shared_ptr<event>      & thread_creation_predecessor()       noexcept { return _predecessors[0]; }
		std::shared_ptr<event> const& thread_creation_predecessor() const noexcept { return _predecessors[0]; }
	};
}
