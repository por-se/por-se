#pragma once

#include "base.h"

#include <cassert>
#include <array>
#include <memory>

namespace por::event {
	class condition_variable_destroy final : public event {
		// predecessors:
		// 1. same-thread predecessor
		// 2. previous acquisition/release of this lock
		std::array<std::shared_ptr<event>, 2> _predecessors;

	protected:
		condition_variable_destroy(thread_id_t tid, std::shared_ptr<event>&& thread_predecessor, std::shared_ptr<event>&& condition_variable_predecessor)
			: event(event_kind::condition_variable_destroy, tid)
			, _predecessors{std::move(thread_predecessor), std::move(condition_variable_predecessor)}
		{
			assert(this->thread_predecessor());
			assert(this->thread_predecessor()->tid() != 0);
			assert(this->thread_predecessor()->tid() == this->tid());
			assert(this->thread_predecessor()->kind() != event_kind::program_init);
			assert(this->thread_predecessor()->kind() != event_kind::thread_exit);
			assert(this->condition_variable_predecessor());
			assert(this->condition_variable_predecessor()->kind() == event_kind::condition_variable_create);
		}

	public:
		static std::shared_ptr<condition_variable_destroy> alloc(thread_id_t tid, std::shared_ptr<event> thread_predecessor, std::shared_ptr<event> condition_variable_predecessor) {
			return std::make_shared<condition_variable_destroy>(condition_variable_destroy{tid, std::move(thread_predecessor), std::move(condition_variable_predecessor)});
		}

		virtual util::iterator_range<std::shared_ptr<event>*> predecessors() override {
			return util::make_iterator_range<std::shared_ptr<event>*>(_predecessors.data(), _predecessors.data() + _predecessors.size());
		}

		std::shared_ptr<event>      & thread_predecessor()       noexcept { return _predecessors[0]; }
		std::shared_ptr<event> const& thread_predecessor() const noexcept { return _predecessors[0]; }

		std::shared_ptr<event>      & condition_variable_predecessor()       noexcept { return _predecessors[1]; }
		std::shared_ptr<event> const& condition_variable_predecessor() const noexcept { return _predecessors[1]; }
	};
}
