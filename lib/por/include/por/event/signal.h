#pragma once

#include "base.h"

#include "por/unfolding.h"

#include <util/sso_array.h>

#include <algorithm>
#include <cassert>

namespace por::event {
	class signal final : public event {
		// predecessors:
		// 1. same-thread predecessor
		// 2. previous same-cond wait1 on notified thread
		// OR (if signal is lost):
		// 1. same-thread predecessor
		// 2+ previous non-lost sig/bro operations (or cond_create) on same condition variable that did not notify this thread (tid of this signal event)
		//    (may be a single nullptr if no such sig/bro events exist and only predecessor is condition_variable_create event, which is optional)
		util::sso_array<event const*, 2> _predecessors; // size = 2: optimizing for the case with a single wait1 (hence the nullptr in the latter case)

		cond_id_t _cid;

	protected:
		signal(thread_id_t tid,
			cond_id_t cid,
			event const& thread_predecessor,
			event const& notified_wait
		)
			: event(event_kind::signal, tid, thread_predecessor, &notified_wait)
			, _predecessors{util::create_uninitialized, 2ul}
			, _cid(cid)
		{
			// we perform a very small optimization by allocating the predecessors in uninitialized storage
			_predecessors[0] = &thread_predecessor;
			_predecessors[1] = &notified_wait;

			assert(this->thread_predecessor());
			assert(this->thread_predecessor()->tid());
			assert(this->thread_predecessor()->tid() == this->tid());
			assert(this->thread_predecessor()->kind() != event_kind::program_init);
			assert(this->thread_predecessor()->kind() != event_kind::thread_exit);

			assert(this->wait_predecessor());
			assert(this->wait_predecessor()->tid());
			assert(this->wait_predecessor()->tid() != this->tid());
			assert(this->wait_predecessor()->kind() == event_kind::wait1);
			assert(this->wait_predecessor()->cid() == this->cid());

			assert(this->condition_variable_predecessors().size() == 1);
			assert(*this->condition_variable_predecessors().begin() == this->wait_predecessor());

			assert(this->cid());

			assert(!this->is_lost());
			assert(this->num_notified() == 1);
			assert(this->notified_thread() == this->wait_predecessor()->tid());
		}

		signal(thread_id_t tid,
			cond_id_t cid,
			event const& thread_predecessor,
			util::iterator_range<event const* const*> condition_variable_predecessors
		)
			: event(event_kind::signal, tid, thread_predecessor, condition_variable_predecessors)
			, _predecessors{util::create_uninitialized, std::max(1ul + condition_variable_predecessors.size(), 2ul)}
			, _cid(cid)
		{
			_predecessors[0] = &thread_predecessor;
			std::size_t index = 1;
			for(auto& c : condition_variable_predecessors) {
				assert(c != nullptr && "no nullptr in cond predecessors allowed");
				_predecessors[index++] = c;
			}
			if(index == 1) {
				_predecessors[index] = nullptr;
			}

			assert(this->thread_predecessor());
			assert(this->thread_predecessor()->tid());
			assert(this->thread_predecessor()->tid() == this->tid());
			assert(this->thread_predecessor()->kind() != event_kind::program_init);
			assert(this->thread_predecessor()->kind() != event_kind::thread_exit);

			assert(!this->wait_predecessor());

			for(auto& e : this->condition_variable_predecessors()) {
				assert(!!e && "range should be empty instead of containing nullptr");
				if(e->kind() == event_kind::signal) {
					[[maybe_unused]] auto sig = static_cast<signal const*>(e);
					assert(!sig->is_lost());
					assert(sig->notified_thread() != this->tid());
					assert(sig->cid() == this->cid());
				} else if(e->kind() == event_kind::broadcast) {
					[[maybe_unused]] auto bro = static_cast<broadcast const*>(e);
					assert(!bro->is_lost());
					assert(!bro->is_notifying_thread(this->tid()));
					assert(bro->cid() == this->cid());
				} else {
					assert(e->kind() == event_kind::condition_variable_create);
					assert(e->cid() == this->cid());
				}
			}

			assert(this->cid());

			assert(this->is_lost());
			assert(this->num_notified() == 0);
			assert(!this->notified_thread());
		}

	public:
		// notifying signal
		static event const& alloc(
			unfolding& unfolding,
			thread_id_t tid,
			cond_id_t cid,
			event const& thread_predecessor,
			event const& notified_thread_predecessor
		) {
			return unfolding.deduplicate(signal{
				tid,
				cid,
				thread_predecessor,
				notified_thread_predecessor
			});
		}

		// lost signal
		static event const& alloc(
			unfolding& unfolding,
			thread_id_t tid,
			cond_id_t cid,
			event const& thread_predecessor,
			std::vector<event const*> cond_predecessors
		) {
			std::sort(cond_predecessors.begin(), cond_predecessors.end());

			return unfolding.deduplicate(signal{
				tid,
				cid,
				thread_predecessor,
				util::iterator_range<event const* const*>(cond_predecessors.data(),
				                                          cond_predecessors.data() + cond_predecessors.size())
			});
		}

		signal(signal&& that)
		: event(std::move(that))
		, _predecessors(std::move(that._predecessors))
		, _cid(that._cid) {
			for(auto& pred : predecessors()) {
				assert(pred != nullptr);
				replace_successor_of(*pred, that);
			}
		}

		~signal() {
			assert(!has_successors());
			for(auto& pred : predecessors()) {
				assert(pred != nullptr);
				remove_from_successors_of(*pred);
			}
		}

		signal() = delete;
		signal(const signal&) = delete;
		signal& operator=(const signal&) = delete;
		signal& operator=(signal&&) = delete;

		std::string to_string(bool details) const override {
			if(details) {
				std::string result = "tid: " + tid().to_string() + " depth: " + std::to_string(depth()) + " kind: signal cid: " + std::to_string(cid()) + " ";
				if(is_lost()) {
					result += "lost";
				} else {
					result += "notifying: " + wait_predecessor()->tid().to_string() + "@" + std::to_string(wait_predecessor()->depth());
				}
				return "[" + result + "]";
			}
			return "signal";
		}

		util::iterator_range<event const* const*> predecessors() const noexcept override {
			if(_predecessors.empty()) {
				return util::make_iterator_range<event const* const*>(nullptr, nullptr);
			} else if(_predecessors.size() > 2 || _predecessors[1] != nullptr) {
				return util::make_iterator_range<event const* const*>(_predecessors.data(), _predecessors.data() + _predecessors.size());
			} else {
				return util::make_iterator_range<event const* const*>(_predecessors.data(), _predecessors.data() + 1);
			}
		}

		event const* thread_predecessor() const override {
			return _predecessors[0];
		}

		// may return nullptr if signal is lost
		event const* wait_predecessor() const noexcept {
			if(!is_lost())
				return _predecessors[1];
			return nullptr;
		}

		// may return empty range if no condition variable predecessor other than condition_variable_create exists
		util::iterator_range<event const* const*> condition_variable_predecessors() const noexcept override {
			if(_predecessors.size() > 2 || _predecessors[1] != nullptr) {
				return util::make_iterator_range<event const* const*>(_predecessors.data() + 1, _predecessors.data() + _predecessors.size());
			} else {
				return util::make_iterator_range<event const* const*>(nullptr, nullptr);
			}
		}

		cond_id_t cid() const noexcept override { return _cid; }

		bool is_lost() const noexcept {
			return _predecessors.size() != 2 || _predecessors[1] == nullptr || _predecessors[1]->kind() != event_kind::wait1;
		}

		std::size_t num_notified() const noexcept {
			return is_lost() ? 0 : 1;
		}

		thread_id_t notified_thread() const noexcept {
			if(_predecessors.size() == 2) {
				if(_predecessors[1] != nullptr && _predecessors[1]->kind() == event_kind::wait1) {
					return _predecessors[1]->tid();
				}
			}
			return thread_id();
		}
	};
}

namespace {
	// wrapper function for broadcast.h
	bool signal_is_lost(por::event::event const* e) {
		assert(e->kind() == por::event::event_kind::signal);
		return static_cast<por::event::signal const*>(e)->is_lost();
	}

	// wrapper function for broadcast.h
	por::event::thread_id_t signal_notified_thread(por::event::event const* e) {
		assert(e->kind() == por::event::event_kind::signal);
		return static_cast<por::event::signal const*>(e)->notified_thread();
	}
}
