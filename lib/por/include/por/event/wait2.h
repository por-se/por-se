#pragma once

#include "base.h"

#include "por/unfolding.h"

#include <cassert>
#include <array>
#include <set>

namespace por::event {
	class wait2 final : public event {
		// predecessors:
		// 1. same-thread predecessor (wait1)
		// 2. signal or broadcast that notified this event
		// 3. previous release of this lock
		std::array<event const*, 3> _predecessors;

		cond_id_t _cid;
		lock_id_t _lid;

	protected:
		wait2(thread_id_t tid,
			cond_id_t cid,
			lock_id_t lid,
			event const& thread_predecessor,
			event const& lock_predecessor,
			event const& condition_variable_predecessor
		)
			: event(event_kind::wait2, tid, thread_predecessor, lock_predecessor, &condition_variable_predecessor)
			, _predecessors{&thread_predecessor, &condition_variable_predecessor, &lock_predecessor}
			, _cid(cid)
			, _lid(lid)
		{
			assert(this->thread_predecessor());
			assert(this->thread_predecessor()->tid());
			assert(this->thread_predecessor()->tid() == this->tid());
			assert(this->thread_predecessor()->kind() == event_kind::wait1);

			assert(this->lock_predecessor());
			assert(this->lock_predecessor()->kind() == event_kind::lock_release || this->lock_predecessor()->kind() == event_kind::wait1);
			assert(this->lock_predecessor()->lid() == this->lid());

			assert(this->thread_predecessor() != this->lock_predecessor());

			assert(this->cid());
			assert(this->lid());

			assert(this->notifying_event());
			assert(this->notifying_event()->tid() != this->tid());
			if(this->notifying_event()->kind() == event_kind::signal) {
				[[maybe_unused]] auto sig = static_cast<signal const*>(this->notifying_event());
				assert(sig->notified_thread() == this->tid());
				assert(sig->cid() == this->cid());
				assert(sig->wait_predecessor() == this->thread_predecessor());
			} else {
				assert(this->notifying_event()->kind() == event_kind::broadcast);
				[[maybe_unused]] auto bro = static_cast<broadcast const*>(this->notifying_event());
				assert(bro->is_notifying_thread(this->tid()));
				assert(bro->cid() == this->cid());
				[[maybe_unused]] bool wait1_found = false;
				for(auto& e : bro->wait_predecessors()) {
					if(e == this->thread_predecessor()) {
						wait1_found = true;
						break;
					}
				}
				assert(wait1_found && "notifying broadcast must wake up corresponding wait1");
			}
		}

	public:
		static por::unfolding::dedupliation_result alloc(
			unfolding& unfolding,
			thread_id_t tid,
			cond_id_t cid,
			lock_id_t lid,
			event const& thread_predecessor,
			event const& lock_predecessor,
			event const& condition_variable_predecessor
		) {
			return unfolding.deduplicate(wait2{
				tid,
				cid,
				lid,
				thread_predecessor,
				lock_predecessor,
				condition_variable_predecessor
			});
		}

		wait2(wait2&& that)
		: event(std::move(that))
		, _predecessors(that._predecessors)
		, _cid(that._cid)
		, _lid(that._lid) {
			that._predecessors = {};
			for(auto& pred : predecessors()) {
				assert(pred != nullptr);
				replace_successor_of(*pred, that);
			}
		}

		~wait2() {
			assert(!has_successors());
			std::set<event const*> P(predecessors().begin(), predecessors().end());
			for(auto& pred : P) {
				assert(pred != nullptr);
				remove_from_successors_of(*pred);
			}
		}

		wait2() = delete;
		wait2(const wait2&) = delete;
		wait2& operator=(const wait2&) = delete;
		wait2& operator=(wait2&&) = delete;

		std::string to_string(bool details) const noexcept override {
			if(details)
				return "[tid: " + tid().to_string() + " depth: " + std::to_string(depth()) + " kind: wait2 cid: " + std::to_string(cid()) + " lid: " + std::to_string(lid()) + (is_cutoff() ? " CUTOFF" : "") + "]";
			return "wait2";
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

		event const* lock_predecessor() const noexcept override { return _predecessors[2]; }

		util::iterator_range<event const* const*> condition_variable_predecessors() const noexcept override {
			return util::make_iterator_range<event const* const*>(_predecessors.data(), _predecessors.data() + 2);
		}

		lock_id_t lid() const noexcept override { return _lid; }
		cond_id_t cid() const noexcept override { return _cid; }

		event const* notifying_event() const noexcept { return _predecessors[1]; }
	};
}
