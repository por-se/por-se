#pragma once

#include "base.h"

#include <util/distance.h>
#include <util/sso_array.h>

#include <algorithm>
#include <cassert>
#include <memory>

namespace por::event {
	class wait1 final : public event {
		// predecessors:
		// 1. same-thread predecessor
		// 2. previous acquisition on same lock
		// 3+ previous lost signals (or cond_create) or broadcasts on same condition variable that did not notify this thread by broadcast
		//    (may not exist if no such events and only preceded by condition_variable_create event)
		util::sso_array<std::shared_ptr<event>, 2> _predecessors;

		cond_id_t _cid;

		exploration_info _info;

	public: // FIXME: should be protected
		template<typename T>
		wait1(thread_id_t tid,
			cond_id_t cid,
			std::shared_ptr<event>&& thread_predecessor,
			std::shared_ptr<event>&& lock_predecessor,
			T&& begin_condition_variable_predecessors,
			T&& end_condition_variable_predecessors
		)
			: event(event_kind::wait1, tid, thread_predecessor, lock_predecessor, util::make_iterator_range<std::shared_ptr<event>*>(begin_condition_variable_predecessors, end_condition_variable_predecessors))
			, _predecessors{util::create_uninitialized, 2ul + util::distance(begin_condition_variable_predecessors, end_condition_variable_predecessors)}
			, _cid(cid)
		{
			// we perform a very small optimization by allocating the predecessors in uninitialized storage
			new(_predecessors.data() + 0) std::shared_ptr<event>(std::move(thread_predecessor));
			new(_predecessors.data() + 1) std::shared_ptr<event>(std::move(lock_predecessor));
			if constexpr(!std::is_same_v<std::decay_t<T>, decltype(nullptr)>) {
				std::size_t index = 2;
				for(auto iter = begin_condition_variable_predecessors; iter != end_condition_variable_predecessors; ++iter, ++index) {
					assert(iter != nullptr);
					assert(*iter != nullptr && "no nullptr in cond predecessors allowed");
					new(_predecessors.data() + index) std::shared_ptr<event>(std::move(*iter));
				}
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
					auto sig = static_cast<signal const*>(e.get());
					assert(sig->is_lost());
					assert(sig->cid() == this->cid());
				} else if(e->kind() == event_kind::broadcast) {
					auto bro = static_cast<broadcast const*>(e.get());
					assert(!bro->is_notifying_thread(this->tid()));
					assert(bro->cid() == this->cid());
				} else {
					assert(e->kind() == event_kind::condition_variable_create);
					assert(static_cast<condition_variable_create const*>(e.get())->cid() == this->cid());
				}
			}
		}

	public:
		template<typename T>
		static std::shared_ptr<event> alloc(
			std::shared_ptr<unfolding>& unfolding,
			thread_id_t tid,
			cond_id_t cid,
			std::shared_ptr<event> thread_predecessor,
			std::shared_ptr<event> lock_predecessor,
			T begin_condition_variable_predecessors,
			T end_condition_variable_predecessors
		) {
			if constexpr(!std::is_same_v<std::decay_t<T>, decltype(nullptr)>) {
				std::sort(begin_condition_variable_predecessors, end_condition_variable_predecessors);
			}
			return deduplicate(unfolding, std::make_shared<wait1>(
				tid,
				cid,
				std::move(thread_predecessor),
				std::move(lock_predecessor),
				std::move(begin_condition_variable_predecessors),
				std::move(end_condition_variable_predecessors)
			));
		}

		virtual void mark_as_open(path_t const& path) const override {
			_info.mark_as_open(path);
		}
		virtual void mark_as_explored(path_t const& path) const override {
			_info.mark_as_explored(path);
		}
		virtual bool is_present(path_t const& path) const override {
			return _info.is_present(path);
		}
		virtual bool is_explored(path_t const& path) const override {
			return _info.is_explored(path);
		}

		virtual std::string to_string(bool details) const override {
			if(details)
				return "[tid: " + std::to_string(tid()) + " depth: " + std::to_string(depth()) + " kind: wait1 cid: " + std::to_string(cid()) +"]";
			return "wait1";
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

		// may return empty range if no condition variable predecessor other than condition_variable_create exists
		util::iterator_range<std::shared_ptr<event>*> condition_variable_predecessors() noexcept {
			return util::make_iterator_range<std::shared_ptr<event>*>(_predecessors.data() + 2, _predecessors.data() + _predecessors.size());
		}
		// may return empty range if no condition variable predecessor other than condition_variable_create exists
		util::iterator_range<std::shared_ptr<event> const*> condition_variable_predecessors() const noexcept {
			return util::make_iterator_range<std::shared_ptr<event> const*>(_predecessors.data() + 2, _predecessors.data() + _predecessors.size());
		}

		cond_id_t cid() const noexcept { return _cid; }
	};
}

namespace {
	// wrapper function for broadcast.h, signal.h
	por::event::cond_id_t wait1_cid(por::event::event const* e) {
		assert(e->kind() == por::event::event_kind::wait1);
		return static_cast<por::event::wait1 const*>(e)->cid();
	}
}
