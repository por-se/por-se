#pragma once

#include "base.h"

#include "por/unfolding.h"

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
			assert(this->cid());
		}

	public:
		static event const& alloc(
			unfolding& unfolding,
			thread_id_t tid,
			cond_id_t cid,
			event const& thread_predecessor
		) {
			return unfolding.deduplicate(condition_variable_create{
				tid,
				cid,
				thread_predecessor
			});
		}

		condition_variable_create(condition_variable_create&& that)
		: event(std::move(that))
		, _predecessors(that._predecessors)
		, _cid(that._cid) {
			that._predecessors = {};
			assert(_predecessors.size() == 1);
			assert(thread_predecessor() != nullptr);
			replace_successor_of(*thread_predecessor(), that);
		}

		~condition_variable_create() {
			assert(!has_successors());
			if(thread_predecessor() != nullptr) {
				remove_from_successors_of(*thread_predecessor());
			}
		}

		condition_variable_create() = delete;
		condition_variable_create(const condition_variable_create&) = delete;
		condition_variable_create& operator=(const condition_variable_create&) = delete;
		condition_variable_create& operator=(condition_variable_create&&) = delete;

		std::string to_string(bool details) const noexcept override {
			if(details)
				return "[tid: " + tid().to_string() + " depth: " + std::to_string(depth()) + " kind: condition_variable_create cid: " + std::to_string(cid()) + "]";
			return "condition_variable_create";
		}

		util::iterator_range<event const* const*> predecessors() const noexcept override {
			if(_predecessors[0] == nullptr) {
				return util::make_iterator_range<event const* const*>(nullptr, nullptr); // only after move-ctor
			}
			return util::make_iterator_range<event const* const*>(_predecessors.data(), _predecessors.data() + _predecessors.size());
		}

		event const* thread_predecessor() const noexcept override {
			return _predecessors[0];
		}

		cond_id_t cid() const noexcept override { return _cid; }
	};
}
