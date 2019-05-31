#pragma once

#include "base.h"

#include <util/distance.h>
#include <util/sso_array.h>

#include <cassert>
#include <memory>

namespace {
	// signal header is not available yet and cannot be included (circular dependency)
	bool signal_is_lost(por::event::event const*);
	por::event::thread_id_t signal_notified_thread(por::event::event const*);
}

namespace por::event {
	class broadcast final : public event {
		// predecessors:
		// 1. same-thread predecessor
		// 2+ previous same-cond wait1 on notified threads
		// X+ previous non-lost signal operations on same condition variable that did not notify any of the threads referenced in any of the wait1s or this thread (tid of broadcast event)
		// OR (if broadcast is lost):
		// 1. same-thread predecessor
		// 2+ previous non-lost sig/bro operations (or cond_create) on same condition variable that did not notify this thread (tid of this broadcast event)
		//    (may not exist if no such events and only preceeded by condition_variable_create event)
		util::sso_array<std::shared_ptr<event>, 0> _predecessors;

		std::size_t num_notified_threads = 0;

	public: // FIXME: should be protected
		template<typename T>
		broadcast(thread_id_t tid,
			std::shared_ptr<event>&& thread_predecessor,
			T&& begin_condition_variable_predecessors,
			T&& end_condition_variable_predecessors
		)
			: event(event_kind::broadcast, tid, thread_predecessor, util::make_iterator_range<std::shared_ptr<event>*>(begin_condition_variable_predecessors, end_condition_variable_predecessors))
			, _predecessors{util::create_uninitialized, 1ul + util::distance(begin_condition_variable_predecessors, end_condition_variable_predecessors)}
		{
			// count events by type
			std::size_t wait1_count = 0;
			std::size_t sigbro_count = 0;
			std::size_t create_count = 0;
			if constexpr(!std::is_same_v<std::decay_t<T>, decltype(nullptr)>) {
				for(auto iter = begin_condition_variable_predecessors; iter != end_condition_variable_predecessors; ++iter) {
					assert(iter != nullptr);
					assert(*iter != nullptr && "no nullptr in cond predecessors allowed");
					switch((*iter)->kind()) {
						case event_kind::condition_variable_create:
							++create_count;
							break;
						case event_kind::wait1:
							++wait1_count;
							break;
						case event_kind::signal:
						case event_kind::broadcast:
							++sigbro_count;
							break;
					}
				}
			}

			// we perform a very small optimization by allocating the predecessors in uninitialized storage
			new(_predecessors.data() + 0) std::shared_ptr<event>(std::move(thread_predecessor));
			if constexpr(!std::is_same_v<std::decay_t<T>, decltype(nullptr)>) {
				std::size_t index = 1;
				if(wait1_count > 0) {
					assert(create_count == 0);
					num_notified_threads = wait1_count;
					// insert wait1 events first
					for(auto iter = begin_condition_variable_predecessors; iter != end_condition_variable_predecessors; ++iter) {
						if((*iter)->kind() == event_kind::wait1) {
							new(_predecessors.data() + index) std::shared_ptr<event>(std::move(*iter));
							*iter = nullptr;
							++index;
						}
					}
					assert(index == 1 + wait1_count);
					// insert sig / bro events last
					for(auto iter = begin_condition_variable_predecessors; iter != end_condition_variable_predecessors; ++iter) {
						if(*iter != nullptr && (*iter)->kind() != event_kind::wait1) {
							assert((*iter)->kind() == event_kind::signal || (*iter)->kind() == event_kind::broadcast);
							new(_predecessors.data() + index) std::shared_ptr<event>(std::move(*iter));
							++index;
						}
					}
				} else {
					assert(create_count <= 1);
					if(sigbro_count >= 0) {
						for(auto iter = begin_condition_variable_predecessors; iter != end_condition_variable_predecessors; ++iter) {
							if((*iter)->kind() == event_kind::condition_variable_create) {
								// insert as last item
								new(_predecessors.data() + _predecessors.size() - 1) std::shared_ptr<event>(std::move(*iter));
							} else {
								assert((*iter)->kind() == event_kind::signal || (*iter)->kind() == event_kind::broadcast);
								new(_predecessors.data() + index) std::shared_ptr<event>(std::move(*iter));
								++index;
							}
						}
					}
					if(create_count == 1) {
						++index; // for assert
					}
				}
				assert(index == _predecessors.size());
			}

			assert(this->thread_predecessor());
			assert(this->thread_predecessor()->tid() != 0);
			assert(this->thread_predecessor()->tid() == this->tid());
			assert(this->thread_predecessor()->kind() != event_kind::program_init);
			assert(this->thread_predecessor()->kind() != event_kind::thread_exit);

			assert(std::distance(this->wait_predecessors().begin(), this->wait_predecessors().end()) == wait1_count);
			for(auto& e : this->wait_predecessors()) {
				assert(e->kind() == event_kind::wait1);
				assert(e->tid() != 0);
				assert(e->tid() != this->tid());
			}

			if(wait1_count > 0) {
				for(auto& e : this->condition_variable_predecessors()) {
					if(e->kind() == event_kind::wait1) {
						assert(e->tid() != this->tid());
					} else {
						assert(e->kind() == event_kind::signal);
						assert(!signal_is_lost(e.get()));
						assert(signal_notified_thread(e.get()) != this->tid());
						for(auto& w : this->wait_predecessors()) {
							assert(signal_notified_thread(e.get()) != w->tid());
						}
					}
				}
			} else {
				for(auto& e : this->condition_variable_predecessors()) {
					if(e->kind() == event_kind::signal) {
						assert(!signal_is_lost(e.get()));
						assert(signal_notified_thread(e.get()) != this->tid());
					} else if(e->kind() == event_kind::broadcast) {
						auto bro = static_cast<broadcast const*>(e.get());
						assert(!bro->is_lost());
						assert(!bro->is_notifying_thread(this->tid()));
					} else {
						assert(e->kind() == event_kind::condition_variable_create);
					}
				}
			}

			// (wait1_count > 0) <=> !is_lost()
			assert(wait1_count > 0 || this->is_lost());
			assert(!this->is_lost() || wait1_count == 0);
		}

	public:
		template<typename T>
		static std::shared_ptr<broadcast> alloc(thread_id_t tid,
			std::shared_ptr<event> thread_predecessor,
			T begin_condition_variable_predecessors,
			T end_condition_variable_predecessors
		) {
			return std::make_shared<broadcast>(tid,
				std::move(thread_predecessor),
				std::move(begin_condition_variable_predecessors),
				std::move(end_condition_variable_predecessors)
			);
		}

		virtual std::string to_string(bool details) const override {
			if(details)
				return "[tid: " + std::to_string(tid()) + " depth: " + std::to_string(depth()) + " kind: broadcast]";
			return "broadcast";
		}

		virtual util::iterator_range<std::shared_ptr<event>*> predecessors() override {
			return util::make_iterator_range<std::shared_ptr<event>*>(_predecessors.data(), _predecessors.data() + _predecessors.size());
		}
		virtual util::iterator_range<std::shared_ptr<event> const*> predecessors() const override {
			return util::make_iterator_range<std::shared_ptr<event> const*>(_predecessors.data(), _predecessors.data() + _predecessors.size());
		}

		std::shared_ptr<event>      & thread_predecessor()       noexcept { return _predecessors[0]; }
		std::shared_ptr<event> const& thread_predecessor() const noexcept { return _predecessors[0]; }

		// may return empty range if no wait predecessor exists (broadcast is lost)
		util::iterator_range<std::shared_ptr<event>*> wait_predecessors() noexcept {
			if(!is_lost()) {
				return util::make_iterator_range<std::shared_ptr<event>*>(_predecessors.data() + 1, _predecessors.data() + 1 + num_notified_threads);
			} else {
				return util::make_iterator_range<std::shared_ptr<event>*>(nullptr, nullptr);
			}
		}
		// may return empty range if no wait predecessor exists (broadcast is lost)
		util::iterator_range<std::shared_ptr<event> const*> wait_predecessors() const noexcept {
			if(!is_lost()) {
				return util::make_iterator_range<std::shared_ptr<event> const*>(_predecessors.data() + 1, _predecessors.data() + 1 + num_notified_threads);
			} else {
				return util::make_iterator_range<std::shared_ptr<event> const*>(nullptr, nullptr);
			}
		}

		// may return empty range if no condition variable predecessor other than condition_variable_create exists
		util::iterator_range<std::shared_ptr<event>*> condition_variable_predecessors() noexcept {
			return util::make_iterator_range<std::shared_ptr<event>*>(_predecessors.data() + 1, _predecessors.data() + _predecessors.size());
		}
		// may return empty range if no condition variable predecessor other than condition_variable_create exists
		util::iterator_range<std::shared_ptr<event> const*> condition_variable_predecessors() const noexcept {
			return util::make_iterator_range<std::shared_ptr<event> const*>(_predecessors.data() + 1, _predecessors.data() + _predecessors.size());
		}

		bool is_lost() const noexcept {
			return num_notified_threads == 0;
		}

		std::size_t num_notified() const noexcept {
			return num_notified_threads;
		}

		thread_id_t is_notifying_thread(thread_id_t tid) const noexcept {
			for(auto& e : wait_predecessors()) {
				if(e->tid() == tid)
					return true;
			}
			return false;
		}
	};
}
