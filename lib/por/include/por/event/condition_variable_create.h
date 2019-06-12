#pragma once

#include "base.h"

#include <cassert>
#include <array>
#include <memory>

namespace por::event {
	class condition_variable_create final : public event {
		// predecessors:
		// 1. same-thread predecessor
		std::array<std::shared_ptr<event>, 1> _predecessors;

		cond_id_t _cid;

	protected:
		condition_variable_create(thread_id_t tid, cond_id_t cid, std::shared_ptr<event>&& thread_predecessor)
			: event(event_kind::condition_variable_create, tid, thread_predecessor)
			, _predecessors{std::move(thread_predecessor)}
			, _cid(cid)
		{
			assert(this->thread_predecessor());
			assert(this->thread_predecessor()->tid() != 0);
			assert(this->thread_predecessor()->tid() == this->tid());
			assert(this->thread_predecessor()->kind() != event_kind::program_init);
			assert(this->thread_predecessor()->kind() != event_kind::thread_exit);
		}

	public:
		static std::shared_ptr<event> alloc(
			std::shared_ptr<unfolding>& unfolding,
			thread_id_t tid,
			cond_id_t cid,
			std::shared_ptr<event> thread_predecessor
		) {
			return deduplicate(unfolding, std::make_shared<condition_variable_create>(
				condition_variable_create{
					tid,
					cid,
					std::move(thread_predecessor)
				}
			));
		}

		virtual std::string to_string(bool details) const override {
			if(details)
				return "[tid: " + std::to_string(tid()) + " depth: " + std::to_string(depth()) + " kind: condition_variable_create cid: " + std::to_string(cid()) +"]";
			return "condition_variable_create";
		}

		virtual util::iterator_range<std::shared_ptr<event>*> predecessors() override {
			return util::make_iterator_range<std::shared_ptr<event>*>(_predecessors.data(), _predecessors.data() + _predecessors.size());
		}
		virtual util::iterator_range<std::shared_ptr<event> const*> predecessors() const override {
			return util::make_iterator_range<std::shared_ptr<event> const*>(_predecessors.data(), _predecessors.data() + _predecessors.size());
		}

		std::shared_ptr<event>      & thread_predecessor()       noexcept { return _predecessors[0]; }
		std::shared_ptr<event> const& thread_predecessor() const noexcept { return _predecessors[0]; }

		cond_id_t cid() const noexcept { return _cid; }
	};
}
