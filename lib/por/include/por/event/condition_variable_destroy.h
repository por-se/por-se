#pragma once

#include "base.h"

#include <util/distance.h>
#include <util/sso_array.h>

#include <cassert>
#include <memory>

namespace por::event {
	class condition_variable_destroy final : public event {
		// predecessors:
		// 1. same-thread predecessor
		// 2+ previous operations on same condition variable
		//    (may not exist if only preceded by condition_variable_create event)
		util::sso_array<std::shared_ptr<event>, 1> _predecessors;

		cond_id_t _cid;

	public: // FIXME: should be protected
		template<typename T>
		condition_variable_destroy(thread_id_t tid,
			cond_id_t cid,
			std::shared_ptr<event>&& thread_predecessor,
			T&& begin_condition_variable_predecessors,
			T&& end_condition_variable_predecessors
		)
			: event(event_kind::condition_variable_destroy, tid, thread_predecessor, util::make_iterator_range<std::shared_ptr<event>*>(begin_condition_variable_predecessors, end_condition_variable_predecessors))
			, _predecessors{util::create_uninitialized, 1ul + util::distance(begin_condition_variable_predecessors, end_condition_variable_predecessors)}
			, _cid(cid)
		{
			// we perform a very small optimization by allocating the predecessors in uninitialized storage
			new(_predecessors.data() + 0) std::shared_ptr<event>(std::move(thread_predecessor));
			if constexpr(!std::is_same_v<std::decay_t<T>, decltype(nullptr)>) {
				std::size_t index = 1;
				for(auto iter = begin_condition_variable_predecessors; iter != end_condition_variable_predecessors; ++iter, ++index) {
					assert(iter != nullptr);
					assert(*iter != nullptr && "no nullptr in cond predecessors allowed");
					new(_predecessors.data() + index) std::shared_ptr<event>(std::move(*iter));
				}
			}

			assert(this->thread_predecessor());
			assert(this->thread_predecessor()->tid() != 0);
			assert(this->thread_predecessor()->tid() == this->tid());
			assert(this->thread_predecessor()->kind() != event_kind::program_init);
			assert(this->thread_predecessor()->kind() != event_kind::thread_exit);

			assert(std::distance(this->condition_variable_predecessors().begin(), this->condition_variable_predecessors().end()) == _predecessors.size() - 1);
			for(auto& e : this->condition_variable_predecessors()) {
				assert(e->kind() != event_kind::wait1 && "destroying a cond that a thread is blocked on is UB");
				assert(e->kind() == event_kind::broadcast
				       || e->kind() == event_kind::signal
				       || e->kind() == event_kind::wait2
				       || e->kind() == event_kind::condition_variable_create);
			}
		}

	public:
		template<typename T>
		static std::shared_ptr<condition_variable_destroy> alloc(thread_id_t tid,
			cond_id_t cid,
			std::shared_ptr<event> thread_predecessor,
			T begin_condition_variable_predecessors,
			T end_condition_variable_predecessors
		) {
			return std::make_shared<condition_variable_destroy>(tid,
				cid,
				std::move(thread_predecessor),
				std::move(begin_condition_variable_predecessors),
				std::move(end_condition_variable_predecessors)
			);
		}

		virtual std::string to_string(bool details) const override {
			if(details)
				return "[tid: " + std::to_string(tid()) + " depth: " + std::to_string(depth()) + " kind: condition_variable_destroy cid: " + std::to_string(cid()) +"]";
			return "condition_variable_destroy";
		}

		virtual util::iterator_range<std::shared_ptr<event>*> predecessors() override {
			return util::make_iterator_range<std::shared_ptr<event>*>(_predecessors.data(), _predecessors.data() + _predecessors.size());
		}
		virtual util::iterator_range<std::shared_ptr<event> const*> predecessors() const override {
			return util::make_iterator_range<std::shared_ptr<event> const*>(_predecessors.data(), _predecessors.data() + _predecessors.size());
		}

		std::shared_ptr<event>      & thread_predecessor()       noexcept { return _predecessors[0]; }
		std::shared_ptr<event> const& thread_predecessor() const noexcept { return _predecessors[0]; }

		// may return empty range if no condition variable predecessor other than condition_variable_create exists
		util::iterator_range<std::shared_ptr<event>*> condition_variable_predecessors() noexcept {
			return util::make_iterator_range<std::shared_ptr<event>*>(_predecessors.data() + 1, _predecessors.data() + _predecessors.size());
		}
		// may return empty range if no condition variable predecessor other than condition_variable_create exists
		util::iterator_range<std::shared_ptr<event> const*> condition_variable_predecessors() const noexcept {
			return util::make_iterator_range<std::shared_ptr<event> const*>(_predecessors.data() + 1, _predecessors.data() + _predecessors.size());
		}

		cond_id_t cid() const noexcept { return _cid; }
	};
}
