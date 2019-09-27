#pragma once

#include "base.h"

#include <cassert>
#include <array>

namespace por::event {
	class condition_variable_create final : public event {
		// predecessors:
		// 1. same-thread predecessor
		std::array<event const*, 1> _predecessors;

		cond_id_t _cid;

	protected:
		condition_variable_create(thread_id_t tid, cond_id_t cid, event const& thread_predecessor)
			: event(event_kind::condition_variable_create, tid, thread_predecessor)
			, _predecessors{&thread_predecessor}
			, _cid(cid)
		{
			assert(this->thread_predecessor());
			assert(this->thread_predecessor()->tid());
			assert(this->thread_predecessor()->tid() == this->tid());
			assert(this->thread_predecessor()->kind() != event_kind::program_init);
			assert(this->thread_predecessor()->kind() != event_kind::thread_exit);
		}

	public:
		static event const& alloc(
			unfolding& unfolding,
			thread_id_t tid,
			cond_id_t cid,
			event const& thread_predecessor
		) {
			return deduplicate(unfolding, condition_variable_create(
				tid,
				cid,
				thread_predecessor
			));
		}

		virtual std::string to_string(bool details) const override {
			if(details)
				return "[tid: " + tid().to_string() + " depth: " + std::to_string(depth()) + " kind: condition_variable_create cid: " + std::to_string(cid()) + "]";
			return "condition_variable_create";
		}

		virtual util::iterator_range<event const* const*> predecessors() const override {
			return util::make_iterator_range<event const* const*>(_predecessors.data(), _predecessors.data() + _predecessors.size());
		}

		virtual event const* thread_predecessor() const override {
			return _predecessors[0];
		}

		cond_id_t cid() const noexcept { return _cid; }
	};
}

namespace {
	// wrapper function for broadcast.h
	por::event::cond_id_t cond_create_cid(por::event::event const* e) {
		assert(e->kind() == por::event::event_kind::condition_variable_create);
		return static_cast<por::event::condition_variable_create const*>(e)->cid();
	}
}
