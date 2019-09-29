#pragma once

#include "base.h"

#include "por/unfolding.h"

#include <util/distance.h>
#include <util/sso_array.h>

#include <algorithm>
#include <cassert>

namespace {
	// defined in signal.h
	por::event::cond_id_t signal_cid(por::event::event const*);

	// defined in wait2.h
	por::event::cond_id_t wait2_cid(por::event::event const*);
}

namespace por::event {
	class condition_variable_destroy final : public event {
		// predecessors:
		// 1. same-thread predecessor
		// 2+ previous operations on same condition variable
		//    (may not exist if only preceded by condition_variable_create event)
		util::sso_array<event const*, 1> _predecessors;

		cond_id_t _cid;

	protected:
		condition_variable_destroy(thread_id_t tid,
			cond_id_t cid,
			event const& thread_predecessor,
			event const* const* begin_condition_variable_predecessors,
			event const* const* end_condition_variable_predecessors
		)
			: event(event_kind::condition_variable_destroy, tid, thread_predecessor, util::make_iterator_range<event const* const*>(begin_condition_variable_predecessors, end_condition_variable_predecessors))
			, _predecessors{util::create_uninitialized, 1ul + util::distance(begin_condition_variable_predecessors, end_condition_variable_predecessors)}
			, _cid(cid)
		{
			_predecessors[0] = &thread_predecessor;

			std::size_t index = 1;
			for(auto iter = begin_condition_variable_predecessors; iter != end_condition_variable_predecessors; ++iter, ++index) {
				assert(*iter != nullptr && "no nullptr in cond predecessors allowed");
				_predecessors[index] = *iter;
			}

			assert(this->thread_predecessor());
			assert(this->thread_predecessor()->tid());
			assert(this->thread_predecessor()->tid() == this->tid());
			assert(this->thread_predecessor()->kind() != event_kind::program_init);
			assert(this->thread_predecessor()->kind() != event_kind::thread_exit);

			assert(std::distance(this->condition_variable_predecessors().begin(), this->condition_variable_predecessors().end()) == _predecessors.size() - 1);
			for(auto& e : this->condition_variable_predecessors()) {
				switch(e->kind()) {
					case event_kind::condition_variable_create:
						assert(static_cast<condition_variable_create const*>(e)->cid() == this->cid());
						break;
					case event_kind::broadcast:
						assert(static_cast<broadcast const*>(e)->cid() == this->cid());
						break;
					case event_kind::signal:
						assert(signal_cid(e) == this->cid());
						break;
					case event_kind::wait1:
						assert(0 && "destroying a cond that a thread is blocked on is UB");
						break;
					case event_kind::wait2:
						assert(wait2_cid(e) == this->cid());
						break;
					default:
						assert(0 && "unexpected event kind in cond predecessors");
				}
			}
		}

	public:
		static event const& alloc(
			unfolding& unfolding,
			thread_id_t tid,
			cond_id_t cid,
			event const& thread_predecessor,
			std::vector<event const*> cond_predecessors
		) {
			std::sort(cond_predecessors.begin(), cond_predecessors.end());

			return unfolding.deduplicate(condition_variable_destroy(
				tid,
				cid,
				thread_predecessor,
				cond_predecessors.data(),
				cond_predecessors.data() + cond_predecessors.size()
			));
		}

		virtual std::string to_string(bool details) const override {
			if(details)
				return "[tid: " + tid().to_string() + " depth: " + std::to_string(depth()) + " kind: condition_variable_destroy cid: " + std::to_string(cid()) + "]";
			return "condition_variable_destroy";
		}

		virtual util::iterator_range<event const* const*> predecessors() const override {
			return util::make_iterator_range<event const* const*>(_predecessors.data(), _predecessors.data() + _predecessors.size());
		}

		virtual event const* thread_predecessor() const override {
			return _predecessors[0];
		}

		// may return empty range if no condition variable predecessor other than condition_variable_create exists
		util::iterator_range<event const* const*> condition_variable_predecessors() const noexcept {
			return util::make_iterator_range<event const* const*>(_predecessors.data() + 1, _predecessors.data() + _predecessors.size());
		}

		cond_id_t cid() const noexcept { return _cid; }
	};
}
