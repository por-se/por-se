#pragma once

#include "base.h"

#include <util/sso_array.h>

#include <cassert>
#include <memory>

namespace por::event {
	class condition_variable_destroy final : public event {
		// predecessors:
		// 1. same-thread predecessor
		// 2. previous operations on same condition variable (may be nullptr if only preceeded by condition_variable_create event)
		util::sso_array<std::shared_ptr<event>, 2> _predecessors;

	public: // FIXME: should be protected
		template<typename T>
		condition_variable_destroy(thread_id_t tid,
			std::shared_ptr<event>&& thread_predecessor,
			T&& begin_condition_variable_predecessors,
			T&& end_condition_variable_predecessors
		)
			: event(event_kind::condition_variable_destroy, tid, thread_predecessor, util::make_iterator_range<std::shared_ptr<event>*>(begin_condition_variable_predecessors, end_condition_variable_predecessors))
			, _predecessors{util::create_uninitialized, 1ul + std::distance(begin_condition_variable_predecessors, end_condition_variable_predecessors)}
		{
			assert(_predecessors.size() >= 2 && "overflow check");

			// we perform a very small optimization by allocating the predecessors in uninitialized storage
			new(_predecessors.data() + 0) std::shared_ptr<event>(std::move(thread_predecessor));
			std::size_t index = 1;
			for(auto iter = begin_condition_variable_predecessors; iter != end_condition_variable_predecessors; ++iter, ++index) {
				assert((*iter)->kind() == event_kind::broadcast
				       || (*iter)->kind() == event_kind::signal
				       || (*iter)->kind() == event_kind::wait1
				       || (*iter)->kind() == event_kind::wait2
				       || (*iter)->kind() == event_kind::condition_variable_create);
				new(_predecessors.data() + index) std::shared_ptr<event>(std::move(*iter));
			}

			assert(index > 1);
			assert(this->thread_predecessor());
			assert(this->thread_predecessor()->tid() != 0);
			assert(this->thread_predecessor()->tid() == this->tid());
			assert(this->thread_predecessor()->kind() != event_kind::program_init);
			assert(this->thread_predecessor()->kind() != event_kind::thread_exit);
		}

	public:
		template<typename T>
		static std::shared_ptr<condition_variable_destroy> alloc(thread_id_t tid,
			std::shared_ptr<event> thread_predecessor,
			T begin_condition_variable_predecessors,
			T end_condition_variable_predecessors
		) {
			return std::make_shared<condition_variable_destroy>(tid,
				std::move(thread_predecessor),
				std::move(begin_condition_variable_predecessors),
				std::move(end_condition_variable_predecessors)
			);
		}

		virtual util::iterator_range<std::shared_ptr<event>*> predecessors() override {
			return util::make_iterator_range<std::shared_ptr<event>*>(_predecessors.data(), _predecessors.data() + _predecessors.size());
		}

		virtual util::iterator_range<std::shared_ptr<event> const*> predecessors() const override {
			return util::make_iterator_range<std::shared_ptr<event> const*>(_predecessors.data(), _predecessors.data() + _predecessors.size());
		}

		std::shared_ptr<event>      & thread_predecessor()       noexcept { return _predecessors[0]; }
		std::shared_ptr<event> const& thread_predecessor() const noexcept { return _predecessors[0]; }
	};
}
