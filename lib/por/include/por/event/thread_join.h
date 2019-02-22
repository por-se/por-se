#pragma once

#include "base.h"
#include "thread_exit.h"

#include <cassert>
#include <array>
#include <memory>

namespace por::event {
	class thread_join final : public event {
		// predecessors:
		// 1. same-thread predecessor
		// 2. joined thread
		std::array<std::shared_ptr<event>, 2> _predecessors;

	protected:
		thread_join(thread_id_t tid, std::shared_ptr<event>&& thread_predecessor, std::shared_ptr<event>&& joined_thread)
			: event(event_kind::thread_join, tid, thread_predecessor, joined_thread)
			, _predecessors{std::move(thread_predecessor), std::move(joined_thread)}
		{
			assert(this->thread_predecessor());
			assert(this->thread_predecessor()->tid() != 0);
			assert(this->thread_predecessor()->tid() == this->tid());
			assert(this->thread_predecessor()->kind() != event_kind::program_init);
			assert(this->thread_predecessor()->kind() != event_kind::thread_exit);
			assert(this->joined_thread());
			assert(this->joined_thread()->kind() == event_kind::thread_exit);
		}

	public:
		static std::shared_ptr<thread_join> alloc(thread_id_t tid, std::shared_ptr<event> thread_predecessor, std::shared_ptr<event> joined_thread) {
			return std::make_shared<thread_join>(thread_join{tid, std::move(thread_predecessor), std::move(joined_thread)});
		}

		virtual util::iterator_range<std::shared_ptr<event>*> predecessors() override {
			return util::make_iterator_range<std::shared_ptr<event>*>(_predecessors.data(), _predecessors.data() + _predecessors.size());
		}

		virtual util::iterator_range<std::shared_ptr<event> const*> predecessors() const override {
			return util::make_iterator_range<std::shared_ptr<event> const*>(_predecessors.data(), _predecessors.data() + _predecessors.size());
		}

		std::shared_ptr<event>      & thread_predecessor()       noexcept { return _predecessors[0]; }
		std::shared_ptr<event> const& thread_predecessor() const noexcept { return _predecessors[0]; }

		std::shared_ptr<event>      & joined_thread()       noexcept { return _predecessors[1]; }
		std::shared_ptr<event> const& joined_thread() const noexcept { return _predecessors[1]; }
	};
}
