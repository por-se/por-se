#pragma once

#include "base.h"

#include <cassert>
#include <array>
#include <memory>

namespace por::event {
	class wait2 final : public event {
		// predecessors:
		// 1. same-thread predecessor
		// 2. previous release of this lock
		// 3. signal or broadcast to trigger this event
		std::array<std::shared_ptr<event>, 3> _predecessors;

	protected:
		template<typename T>
		wait2(thread_id_t tid,
			std::shared_ptr<event>&& thread_predecessor,
			std::shared_ptr<event>&& lock_predecessor,
			std::shared_ptr<event>&& condition_variable_predecessor
		)
			: event(event_kind::wait2, tid)
			, _predecessors{std::move(thread_predecessor), std::move(lock_predecessor), std::move(condition_variable_predecessor)}
		{
			assert(this->thread_predecessor());
			assert(this->thread_predecessor()->tid() != 0);
			assert(this->thread_predecessor()->tid() == this->tid());
			assert(this->thread_predecessor()->kind() != event_kind::program_init);
			assert(this->thread_predecessor()->kind() != event_kind::thread_exit);
			assert(this->lock_predecessor());
			assert(
				this->lock_predecessor()->kind() == event_kind::lock_acquire
				&& this->lock_predecessor()->tid() == this->tid()
			);
			assert(this->condition_variable_predecessor());
			assert(this->condition_variable_predecessor()->tid() != this->tid());
			assert(this->condition_variable_predecessor()->kind() == event_kind::signal || this->condition_variable_predecessor()->kind() == event_kind::broadcast);
		}

	public:
		template<typename T>
		static std::shared_ptr<wait2> alloc(thread_id_t tid,
			std::shared_ptr<event> thread_predecessor,
			std::shared_ptr<event> lock_predecessor,
			std::shared_ptr<event> condition_variable_predecessor
		) {
			return std::make_shared<wait2>(wait2{tid,
				std::move(thread_predecessor),
				std::move(lock_predecessor),
				std::move(condition_variable_predecessor)
			});
		}

		virtual util::iterator_range<std::shared_ptr<event>*> predecessors() override {
			return util::make_iterator_range<std::shared_ptr<event>*>(_predecessors.data(), _predecessors.data() + _predecessors.size());
		}

		virtual util::iterator_range<std::shared_ptr<event> const*> predecessors() const override {
			return util::make_iterator_range<std::shared_ptr<event> const*>(_predecessors.data(), _predecessors.data() + _predecessors.size());
		}

		std::shared_ptr<event>      & thread_predecessor()       noexcept { return _predecessors[0]; }
		std::shared_ptr<event> const& thread_predecessor() const noexcept { return _predecessors[0]; }

		std::shared_ptr<event>      & lock_predecessor()       noexcept { return _predecessors[1]; }
		std::shared_ptr<event> const& lock_predecessor() const noexcept { return _predecessors[1]; }

		std::shared_ptr<event>      & condition_variable_predecessor()       noexcept { return _predecessors[2]; }
		std::shared_ptr<event> const& condition_variable_predecessor() const noexcept { return _predecessors[2]; }
	};
}
