#pragma once

#include "base.h"

#include <util/distance.h>
#include <util/sso_array.h>

#include <cassert>
#include <memory>

namespace por::event {
	class signal final : public event {
		// predecessors:
		// 1. same-thread predecessor
		// 2. previous same-cond wait1 on notified thread
		// OR (if signal is lost):
		// 1. same-thread predecessor
		// 2+ previous non-lost sig/bro operations (or cond_create) on same condition variable that did not notify this thread (tid of this signal event)
		//    (may be a single nullptr if no such sig/bro events exist and only predecessor is condition_variable_create event, which is optional)
		util::sso_array<std::shared_ptr<event>, 2> _predecessors; // size = 2: optimizing for the case with a single wait1 (hence the nullptr in the latter case)

		cond_id_t _cid;

		exploration_info _info;

	public: // FIXME: should be protected
		signal(thread_id_t tid,
			cond_id_t cid,
			std::shared_ptr<event>&& thread_predecessor,
			std::shared_ptr<event>&& notified_wait
		)
			: event(event_kind::signal, tid, thread_predecessor, notified_wait)
			, _predecessors{util::create_uninitialized, 2ul}
			, _cid(cid)
		{
			// we perform a very small optimization by allocating the predecessors in uninitialized storage
			new(_predecessors.data() + 0) std::shared_ptr<event>(std::move(thread_predecessor));
			new(_predecessors.data() + 1) std::shared_ptr<event>(std::move(notified_wait));

			assert(this->thread_predecessor());
			assert(this->thread_predecessor()->tid() != 0);
			assert(this->thread_predecessor()->tid() == this->tid());
			assert(this->thread_predecessor()->kind() != event_kind::program_init);
			assert(this->thread_predecessor()->kind() != event_kind::thread_exit);

			assert(this->wait_predecessor());
			assert(this->wait_predecessor()->tid() != 0);
			assert(this->wait_predecessor()->tid() != this->tid());
			assert(this->wait_predecessor()->kind() == event_kind::wait1);

			assert(std::distance(this->condition_variable_predecessors().begin(), this->condition_variable_predecessors().end()) == 1);
			assert(*this->condition_variable_predecessors().begin() == this->wait_predecessor());

			assert(!this->is_lost());
			assert(this->num_notified() == 1);
			assert(this->notified_thread() == this->wait_predecessor()->tid());
		}

		template<typename T>
		signal(thread_id_t tid,
			cond_id_t cid,
			std::shared_ptr<event>&& thread_predecessor,
			T&& begin_condition_variable_predecessors,
			T&& end_condition_variable_predecessors
		)
			: event(event_kind::signal, tid, thread_predecessor, util::make_iterator_range<std::shared_ptr<event>*>(begin_condition_variable_predecessors, end_condition_variable_predecessors))
			, _predecessors{util::create_uninitialized, 1ul + util::distance(begin_condition_variable_predecessors, end_condition_variable_predecessors)}
			, _cid(cid)
		{
			// we perform a very small optimization by allocating the predecessors in uninitialized storage
			new(_predecessors.data() + 0) std::shared_ptr<event>(std::move(thread_predecessor));
			std::size_t index = 1;
			if constexpr(!std::is_same_v<std::decay_t<T>, decltype(nullptr)>) {
				for(auto iter = begin_condition_variable_predecessors; iter != end_condition_variable_predecessors; ++iter, ++index) {
					assert(iter != nullptr);
					assert(*iter != nullptr && "no nullptr in cond predecessors allowed");
					new(_predecessors.data() + index) std::shared_ptr<event>(std::move(*iter));
				}
			}
			if(index == 1) {
				new(_predecessors.data() + index) std::shared_ptr<event>(nullptr);
			}

			assert(this->thread_predecessor());
			assert(this->thread_predecessor()->tid() != 0);
			assert(this->thread_predecessor()->tid() == this->tid());
			assert(this->thread_predecessor()->kind() != event_kind::program_init);
			assert(this->thread_predecessor()->kind() != event_kind::thread_exit);

			assert(!this->wait_predecessor());

			for(auto& e : this->condition_variable_predecessors()) {
				assert(!!e && "range should be empty instead of containing nullptr");
				if(e->kind() == event_kind::signal) {
					auto sig = static_cast<signal const*>(e.get());
					assert(!sig->is_lost());
					assert(sig->notified_thread() != this->tid());
				} else if(e->kind() == event_kind::broadcast) {
					auto bro = static_cast<broadcast const*>(e.get());
					assert(!bro->is_lost());
					assert(!bro->is_notifying_thread(this->tid()));
				} else {
					assert(e->kind() == event_kind::condition_variable_create);
				}
			}

			assert(this->is_lost());
			assert(this->num_notified() == 0);
			assert(this->notified_thread() == 0);
		}

	public:
		// notifying signal
		static std::shared_ptr<event> alloc(
			std::shared_ptr<unfolding>& unfolding,
			thread_id_t tid,
			cond_id_t cid,
			std::shared_ptr<event> thread_predecessor,
			std::shared_ptr<event> notified_thread_predecessor
		) {
			return deduplicate(unfolding, std::make_shared<signal>(
				tid,
				cid,
				std::move(thread_predecessor),
				std::move(notified_thread_predecessor)
			));
		}

		// lost signal
		template<typename T>
		static std::shared_ptr<event> alloc(
			std::shared_ptr<unfolding>& unfolding,
			thread_id_t tid,
			cond_id_t cid,
			std::shared_ptr<event> thread_predecessor,
			T begin_condition_variable_predecessors,
			T end_condition_variable_predecessors
		) {
			return deduplicate(unfolding, std::make_shared<signal>(
				tid,
				cid,
				std::move(thread_predecessor),
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
				return "[tid: " + std::to_string(tid()) + " depth: " + std::to_string(depth()) + " kind: signal cid: " + std::to_string(cid()) +"]";
			return "signal";
		}

		virtual util::iterator_range<std::shared_ptr<event>*> predecessors() override {
			if(_predecessors.size() > 2 || _predecessors[1] != nullptr) {
				return util::make_iterator_range<std::shared_ptr<event>*>(_predecessors.data(), _predecessors.data() + _predecessors.size());
			} else {
				return util::make_iterator_range<std::shared_ptr<event>*>(_predecessors.data(), _predecessors.data() + 1);
			}
		}
		virtual util::iterator_range<std::shared_ptr<event> const*> predecessors() const override {
			if(_predecessors.size() > 2 || _predecessors[1] != nullptr) {
				return util::make_iterator_range<std::shared_ptr<event> const*>(_predecessors.data(), _predecessors.data() + _predecessors.size());
			} else {
				return util::make_iterator_range<std::shared_ptr<event> const*>(_predecessors.data(), _predecessors.data() + 1);
			}
		}

		std::shared_ptr<event>      & thread_predecessor()       noexcept { return _predecessors[0]; }
		std::shared_ptr<event> const& thread_predecessor() const noexcept { return _predecessors[0]; }

		// may return nullptr if signal is lost
		std::shared_ptr<event>       wait_predecessor()       noexcept {
			if(!is_lost())
				return _predecessors[1];
			return nullptr;
		}
		// may return nullptr if signal is lost
		std::shared_ptr<event> const wait_predecessor() const noexcept {
			if(!is_lost())
				return _predecessors[1];
			return nullptr;
		}

		// may return empty range if no condition variable predecessor other than condition_variable_create exists
		util::iterator_range<std::shared_ptr<event>*> condition_variable_predecessors() noexcept {
			if(_predecessors.size() > 2 || _predecessors[1] != nullptr) {
				return util::make_iterator_range<std::shared_ptr<event>*>(_predecessors.data() + 1, _predecessors.data() + _predecessors.size());
			} else {
				return util::make_iterator_range<std::shared_ptr<event>*>(nullptr, nullptr);
			}
		}
		// may return empty range if no condition variable predecessor other than condition_variable_create exists
		util::iterator_range<std::shared_ptr<event> const*> condition_variable_predecessors() const noexcept {
			if(_predecessors.size() > 2 || _predecessors[1] != nullptr) {
				return util::make_iterator_range<std::shared_ptr<event> const*>(_predecessors.data() + 1, _predecessors.data() + _predecessors.size());
			} else {
				return util::make_iterator_range<std::shared_ptr<event> const*>(nullptr, nullptr);
			}
		}

		cond_id_t cid() const noexcept { return _cid; }

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
			return 0;
		}
	};
}

namespace {
	// wrapper functions for broadcast.h
	bool signal_is_lost(por::event::event const* e) {
		assert(e->kind() == por::event::event_kind::signal);
		return static_cast<por::event::signal const*>(e)->is_lost();
	}

	por::event::thread_id_t signal_notified_thread(por::event::event const* e) {
		assert(e->kind() == por::event::event_kind::signal);
		return static_cast<por::event::signal const*>(e)->notified_thread();
	}
}
