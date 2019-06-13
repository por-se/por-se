#pragma once

#include "base.h"

#include <cassert>
#include <array>
#include <memory>

namespace por::event {
	class wait2 final : public event {
		// predecessors:
		// 1. same-thread predecessor (wait1)
		// 2. signal or broadcast that notified this event
		// 3. previous release of this lock
		std::array<std::shared_ptr<event>, 3> _predecessors;

		cond_id_t _cid;

		exploration_info _info;

	protected:
		wait2(thread_id_t tid,
			cond_id_t cid,
			std::shared_ptr<event>&& thread_predecessor,
			std::shared_ptr<event>&& lock_predecessor,
			std::shared_ptr<event>&& condition_variable_predecessor
		)
			: event(event_kind::wait2, tid, thread_predecessor, lock_predecessor, condition_variable_predecessor)
			, _predecessors{std::move(thread_predecessor), std::move(condition_variable_predecessor), std::move(lock_predecessor)}
			, _cid(cid)
		{
			assert(this->thread_predecessor());
			assert(this->thread_predecessor()->tid() != 0);
			assert(this->thread_predecessor()->tid() == this->tid());
			assert(this->thread_predecessor()->kind() == event_kind::wait1);

			assert(this->lock_predecessor());
			assert(this->lock_predecessor()->kind() == event_kind::lock_release || this->lock_predecessor()->kind() == event_kind::wait1);

			assert(this->notifying_event());
			assert(this->notifying_event()->tid() != this->tid());
			if(this->notifying_event()->kind() == event_kind::signal) {
				auto sig = static_cast<signal const*>(this->notifying_event().get());
				assert(sig->notified_thread() == this->tid());
			} else {
				assert(this->notifying_event()->kind() == event_kind::broadcast);
				auto bro = static_cast<broadcast const*>(this->notifying_event().get());
				assert(bro->is_notifying_thread(this->tid()));
			}
		}

	public:
		static std::shared_ptr<event> alloc(
			std::shared_ptr<unfolding>& unfolding,
			thread_id_t tid,
			cond_id_t cid,
			std::shared_ptr<event> thread_predecessor,
			std::shared_ptr<event> lock_predecessor,
			std::shared_ptr<event> condition_variable_predecessor
		) {
			return deduplicate(unfolding, std::make_shared<wait2>(
				wait2{
					tid,
					cid,
					std::move(thread_predecessor),
					std::move(lock_predecessor),
					std::move(condition_variable_predecessor)
				}
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
				return "[tid: " + std::to_string(tid()) + " depth: " + std::to_string(depth()) + " kind: wait2 cid: " + std::to_string(cid()) +"]";
			return "wait2";
		}

		virtual util::iterator_range<std::shared_ptr<event>*> predecessors() override {
			return util::make_iterator_range<std::shared_ptr<event>*>(_predecessors.data(), _predecessors.data() + _predecessors.size());
		}
		virtual util::iterator_range<std::shared_ptr<event> const*> predecessors() const override {
			return util::make_iterator_range<std::shared_ptr<event> const*>(_predecessors.data(), _predecessors.data() + _predecessors.size());
		}

		std::shared_ptr<event>      & thread_predecessor()       noexcept { return _predecessors[0]; }
		std::shared_ptr<event> const& thread_predecessor() const noexcept { return _predecessors[0]; }

		std::shared_ptr<event>      & lock_predecessor()       noexcept { return _predecessors[2]; }
		std::shared_ptr<event> const& lock_predecessor() const noexcept { return _predecessors[2]; }

		util::iterator_range<std::shared_ptr<event>*> condition_variable_predecessors() noexcept {
			return util::make_iterator_range<std::shared_ptr<event>*>(_predecessors.data(), _predecessors.data() + 2);
		}
		util::iterator_range<std::shared_ptr<event> const*> condition_variable_predecessors() const noexcept {
			return util::make_iterator_range<std::shared_ptr<event> const*>(_predecessors.data(), _predecessors.data() + 2);
		}

		cond_id_t cid() const noexcept { return _cid; }

		std::shared_ptr<event>      & notifying_event()       noexcept { return _predecessors[1]; }
		std::shared_ptr<event> const& notifying_event() const noexcept { return _predecessors[1]; }
	};
}
