#pragma once

#include "base.h"

#include "por/unfolding.h"

#include <util/distance.h>
#include <util/sso_array.h>

#include <algorithm>
#include <cassert>

namespace por::event {
	class wait1 final : public event {
		// predecessors:
		// 1. same-thread predecessor
		// 2. previous acquisition on same lock
		// 3+ previous lost signals (or cond_create) or broadcasts on same condition variable that did not notify this thread by broadcast
		//    (may not exist if no such events and only preceded by condition_variable_create event)
		util::sso_array<event const*, 2> _predecessors;

		cond_id_t _cid;

		exploration_info _info;

	protected:
		wait1(thread_id_t tid,
			cond_id_t cid,
			event const& thread_predecessor,
			event const& lock_predecessor,
			event const* const* begin_condition_variable_predecessors,
			event const* const* end_condition_variable_predecessors
		)
			: event(event_kind::wait1, tid, thread_predecessor, &lock_predecessor, util::make_iterator_range<event const* const*>(begin_condition_variable_predecessors, end_condition_variable_predecessors))
			, _predecessors{util::create_uninitialized, 2ul + util::distance(begin_condition_variable_predecessors, end_condition_variable_predecessors)}
			, _cid(cid)
		{
			_predecessors[0] = &thread_predecessor;
			_predecessors[1] = &lock_predecessor;
			std::size_t index = 2;
			for(auto iter = begin_condition_variable_predecessors; iter != end_condition_variable_predecessors; ++iter, ++index) {
				assert(*iter != nullptr && "no nullptr in cond predecessors allowed");
				_predecessors[index] = *iter;
			}

			assert(this->thread_predecessor());
			assert(this->thread_predecessor()->tid());
			assert(this->thread_predecessor()->tid() == this->tid());
			assert(this->thread_predecessor()->kind() != event_kind::program_init);
			assert(this->thread_predecessor()->kind() != event_kind::thread_exit);

			assert(this->lock_predecessor());
			assert(this->lock_predecessor()->kind() == event_kind::lock_acquire || this->lock_predecessor()->kind() == event_kind::wait2);
			assert(this->lock_predecessor()->tid() == this->tid());

			for(auto& e : this->condition_variable_predecessors()) {
				if(e->kind() == event_kind::signal) {
					[[maybe_unused]] auto sig = static_cast<signal const*>(e);
					assert(sig->is_lost());
					assert(sig->cid() == this->cid());
				} else if(e->kind() == event_kind::broadcast) {
					[[maybe_unused]] auto bro = static_cast<broadcast const*>(e);
					assert(!bro->is_notifying_thread(this->tid()));
					assert(bro->cid() == this->cid());
				} else {
					assert(e->kind() == event_kind::condition_variable_create);
					assert(e->cid() == this->cid());
				}
			}

			assert(this->cid());
		}

	public:
		static event const& alloc(
			unfolding& unfolding,
			thread_id_t tid,
			cond_id_t cid,
			event const& thread_predecessor,
			event const& lock_predecessor,
			std::vector<event const*> cond_predecessors
		) {
			std::sort(cond_predecessors.begin(), cond_predecessors.end());

			return unfolding.deduplicate(wait1{
				tid,
				cid,
				thread_predecessor,
				lock_predecessor,
				cond_predecessors.data(),
				cond_predecessors.data() + cond_predecessors.size()
			});
		}

		void mark_as_open(path_t const& path) const override {
			_info.mark_as_open(path);
		}
		void mark_as_explored(path_t const& path) const override {
			_info.mark_as_explored(path);
		}
		bool is_present(path_t const& path) const override {
			return _info.is_present(path);
		}
		bool is_explored(path_t const& path) const override {
			return _info.is_explored(path);
		}

		std::string to_string(bool details) const override {
			if(details)
				return "[tid: " + tid().to_string() + " depth: " + std::to_string(depth()) + " kind: wait1 cid: " + std::to_string(cid()) + "]";
			return "wait1";
		}

		util::iterator_range<event const* const*> predecessors() const noexcept override {
			return util::make_iterator_range<event const* const*>(_predecessors.data(), _predecessors.data() + _predecessors.size());
		}

		event const* thread_predecessor() const override {
			return _predecessors[0];
		}

		event const* lock_predecessor() const noexcept override { return _predecessors[1]; }

		// may return empty range if no condition variable predecessor other than condition_variable_create exists
		util::iterator_range<event const* const*> condition_variable_predecessors() const noexcept override {
			return util::make_iterator_range<event const* const*>(_predecessors.data() + 2, _predecessors.data() + _predecessors.size());
		}

		cond_id_t cid() const noexcept override { return _cid; }
	};
}
