#pragma once

#include "base.h"

#include "util/sso_array.h"

#include <algorithm>
#include <cassert>
#include <memory>

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
			util::iterator_range<event const* const*> condition_variable_predecessors
		)
			: event(event_kind::condition_variable_destroy, tid, thread_predecessor, condition_variable_predecessors)
			, _predecessors{util::create_uninitialized, 1ul + condition_variable_predecessors.size()}
			, _cid(cid)
		{
			_predecessors[0] = &thread_predecessor;

			std::size_t index = 1;
			for(auto& c : condition_variable_predecessors) {
				assert(c != nullptr && "no nullptr in cond predecessors allowed");
				_predecessors[index++] = c;
			}

			assert(this->thread_predecessor());
			assert(this->thread_predecessor()->tid());
			assert(this->thread_predecessor()->tid() == this->tid());
			assert(this->thread_predecessor()->kind() != event_kind::program_init);
			assert(this->thread_predecessor()->kind() != event_kind::thread_exit);

			assert(this->condition_variable_predecessors().size() == _predecessors.size() - 1);
			for(auto& e : this->condition_variable_predecessors()) {
				switch(e->kind()) {
					case event_kind::condition_variable_create:
					case event_kind::broadcast:
					case event_kind::signal:
					case event_kind::wait2:
						assert(e->cid() == this->cid());
						break;
					case event_kind::wait1:
						assert(0 && "destroying a cond that a thread is blocked on is UB");
						break;
					default:
						assert(0 && "unexpected event kind in cond predecessors");
				}
			}

			assert(this->cid());
		}

	public:
		static std::unique_ptr<por::event::event> alloc(
			thread_id_t tid,
			cond_id_t cid,
			event const& thread_predecessor,
			std::vector<event const*> cond_predecessors
		) {
			std::sort(cond_predecessors.begin(), cond_predecessors.end());

			return std::make_unique<condition_variable_destroy>(condition_variable_destroy{
				tid,
				cid,
				thread_predecessor,
				util::make_iterator_range<event const* const*>(cond_predecessors.data(),
				                                               cond_predecessors.data() + cond_predecessors.size())
			});
		}

		condition_variable_destroy(condition_variable_destroy&& that)
		: event(std::move(that))
		, _predecessors(std::move(that._predecessors))
		, _cid(that._cid)
		{ }

		~condition_variable_destroy() {
			assert(!has_successors());
			for(auto& pred : immediate_predecessors_from_cone()) {
				assert(pred != nullptr);
				remove_from_successors_of(*pred);
			}
		}

		condition_variable_destroy() = delete;
		condition_variable_destroy(const condition_variable_destroy&) = delete;
		condition_variable_destroy& operator=(const condition_variable_destroy&) = delete;
		condition_variable_destroy& operator=(condition_variable_destroy&&) = delete;

		std::string to_string(bool details) const noexcept override {
			if(details)
				return "[tid: " + tid().to_string() + " depth: " + std::to_string(depth()) + " kind: condition_variable_destroy cid: " + std::to_string(cid()) + (is_cutoff() ? " CUTOFF" : "") + "]";
			return "condition_variable_destroy";
		}

		util::iterator_range<event const* const*> predecessors() const noexcept override {
			return util::make_iterator_range<event const* const*>(_predecessors.data(), _predecessors.data() + _predecessors.size());
		}

		event const* thread_predecessor() const noexcept override {
			return _predecessors[0];
		}

		// may return empty range if no condition variable predecessor other than condition_variable_create exists
		util::iterator_range<event const* const*> condition_variable_predecessors() const noexcept override {
			return util::make_iterator_range<event const* const*>(_predecessors.data() + 1, _predecessors.data() + _predecessors.size());
		}

		cond_id_t cid() const noexcept override { return _cid; }
	};
}
