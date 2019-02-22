#pragma once

#include "base.h"

#include <util/sso_array.h>

#include <cassert>
#include <memory>

namespace por::event {
	class wait1 final : public event {
		// predecessors:
		// 1. same-thread predecessor
		// 2. previous acquisition of this lock
		// 3+ previous operation on the condition variable
		util::sso_array<std::shared_ptr<event>, 3> _predecessors;

	protected:
		template<typename T>
		wait1(thread_id_t tid,
			std::shared_ptr<event>&& thread_predecessor,
			std::shared_ptr<event>&& lock_predecessor,
			T&& begin_condition_variable_predecessors,
			T&& end_condition_variable_predecessors
		)
			: event(event_kind::wait1, tid)
			, _predecessors{util::create_uninitialized, 2 + std::distance(begin_condition_variable_predecessors, end_condition_variable_predecessors)}
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
			assert(_predecessors.size() >= 2 && "overflow check");

			// we perform a very small optimization by allocating the predecessors in uninitialized storage
			new(_predecessors.data() + 0) std::shared_ptr<event>(std::move(thread_predecessor));
			new(_predecessors.data() + 1) std::shared_ptr<event>(std::move(lock_predecessor));
			auto index = static_cast<std::size_t>(2);
			for(auto iter = begin_condition_variable_predecessors; iter != end_condition_variable_predecessors; ++iter, ++index) {
				new(_predecessors.data() + index) std::shared_ptr<event>(std::move(lock_predecessor));
			}
		}

	public:
		template<typename T>
		static std::shared_ptr<wait1> alloc(thread_id_t tid,
			std::shared_ptr<event> thread_predecessor,
			std::shared_ptr<event> lock_predecessor,
			T begin_condition_variable_predecessors,
			T end_condition_variable_predecessors
		) {
			return std::make_shared<wait1>(wait1{tid,
				std::move(thread_predecessor),
				std::move(lock_predecessor),
				std::move(begin_condition_variable_predecessors),
				std::move(end_condition_variable_predecessors)
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

		util::iterator_range<std::shared_ptr<event>*> condition_variable_predecessors() noexcept {
			assert(_predecessors.size() >= 2);
			return util::make_iterator_range<std::shared_ptr<event>*>(_predecessors.data() + 2, _predecessors.data() + _predecessors.size());
		}
		util::iterator_range<std::shared_ptr<event> const*> condition_variable_predecessors() const noexcept {
			assert(_predecessors.size() >= 2);
			return util::make_iterator_range<std::shared_ptr<event> const*>(_predecessors.data() + 2, _predecessors.data() + _predecessors.size());
		}
	};
}
