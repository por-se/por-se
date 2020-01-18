#pragma once

#include "base.h"

#include "por/unfolding.h"

#include <util/sso_array.h>

#include <algorithm>
#include <cassert>
#include <set>

namespace {
	// defined in signal.h
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
		//    (may not exist if no such events and only preceded by condition_variable_create event)
		util::sso_array<event const*, 0> _predecessors;

		std::size_t _num_notified_threads = 0;

		cond_id_t _cid;

	protected:
		broadcast(thread_id_t tid,
			cond_id_t cid,
			event const& thread_predecessor,
			util::iterator_range<event const* const*> condition_variable_predecessors
		)
			: event(event_kind::broadcast, tid, thread_predecessor, condition_variable_predecessors)
			, _predecessors{util::create_uninitialized, 1ul + condition_variable_predecessors.size()}
			, _cid(cid)
		{
			// group events by type
			std::vector<event const*> wait1;
			std::vector<event const*> sigbro;
			std::vector<event const*> create;
			for(auto& c : condition_variable_predecessors) {
				assert(c != nullptr && "no nullptr in cond predecessors allowed");
				switch(c->kind()) {
					case event_kind::condition_variable_create:
						assert(c->cid() == this->cid());
						create.push_back(c);
						break;
					case event_kind::wait1:
						assert(c->cid() == this->cid());
						wait1.push_back(c);
						break;
					case event_kind::signal:
						assert(c->cid() == this->cid());
						sigbro.push_back(c);
						break;
					case event_kind::broadcast:
						assert(c->cid() == this->cid());
						sigbro.push_back(c);
						break;
					default:
						assert(0 && "unexpected event kind in cond predecessors");
				}
			}

			_predecessors[0] = &thread_predecessor;
			std::size_t index = 1;
			if(wait1.size() > 0) {
				assert(create.empty());
				_num_notified_threads = wait1.size();
				// insert wait1 events first
				for(auto& e : wait1) {
					_predecessors[index++] = e;
				}
			}
			assert(index == 1 + wait1.size());
			// insert sig / bro events
			for(auto& e : sigbro) {
				_predecessors[index++] = e;
			}
			// insert create event as last item
			for(auto& e : create) {
				assert(create.size() == 1);
				_predecessors[index++] = e;
			}
			assert(index == _predecessors.size());

			assert(this->cid());

			assert(this->thread_predecessor());
			assert(this->thread_predecessor()->tid());
			assert(this->thread_predecessor()->tid() == this->tid());
			assert(this->thread_predecessor()->kind() != event_kind::program_init);
			assert(this->thread_predecessor()->kind() != event_kind::thread_exit);

			assert(this->wait_predecessors().size() == wait1.size());
			for(auto& e : this->wait_predecessors()) {
				assert(e->kind() == event_kind::wait1);
				assert(e->tid());
				assert(e->tid() != this->tid());
			}

			if(wait1.size() > 0) {
				for(auto& e : this->condition_variable_predecessors()) {
					if(e->kind() == event_kind::wait1) {
						assert(e->tid() != this->tid());
					} else {
						assert(e->kind() == event_kind::signal);
						assert(!signal_is_lost(e));
						assert(signal_notified_thread(e) != this->tid());
						for(auto& w : this->wait_predecessors()) {
							assert(signal_notified_thread(e) != w->tid());
						}
					}
				}
			} else {
				for(auto& e : this->condition_variable_predecessors()) {
					if(e->kind() == event_kind::signal) {
						assert(!signal_is_lost(e));
						assert(signal_notified_thread(e) != this->tid());
					} else if(e->kind() == event_kind::broadcast) {
						[[maybe_unused]] auto bro = static_cast<broadcast const*>(e);
						assert(!bro->is_lost());
						assert(!bro->is_notifying_thread(this->tid()));
					} else {
						assert(e->kind() == event_kind::condition_variable_create);
					}
				}
			}

			// (wait1_count > 0) <=> !is_lost()
			assert(wait1.size() > 0 || this->is_lost());
			assert(!this->is_lost() || wait1.empty());
		}

	public:
		static por::unfolding::deduplication_result alloc(
			unfolding& unfolding,
			thread_id_t tid,
			cond_id_t cid,
			event const& thread_predecessor,
			std::vector<event const*> cond_predecessors
		) {
			std::sort(cond_predecessors.begin(), cond_predecessors.end());

			return unfolding.deduplicate(broadcast{
				tid,
				cid,
				thread_predecessor,
				util::make_iterator_range<event const* const*>(cond_predecessors.data(),
				                                               cond_predecessors.data() + cond_predecessors.size())
			});
		}

		broadcast(broadcast&& that)
		: event(std::move(that))
		, _predecessors(std::move(that._predecessors))
		, _num_notified_threads(that._num_notified_threads)
		, _cid(that._cid) {
			for(auto& pred : predecessors()) {
				assert(pred != nullptr);
				replace_successor_of(*pred, that);
			}
		}

		~broadcast() {
			assert(!has_successors());
			std::set<event const*> P(predecessors().begin(), predecessors().end());
			for(auto& pred : P) {
				assert(pred != nullptr);
				remove_from_successors_of(*pred);
			}
		}

		broadcast() = delete;
		broadcast(const broadcast&) = delete;
		broadcast& operator=(const broadcast&) = delete;
		broadcast& operator=(broadcast&&) = delete;

		std::string to_string(bool details) const noexcept override {
			if(details) {
				std::string result = "tid: " + tid().to_string() + " depth: " + std::to_string(depth()) + " kind: broadcast cid: " + std::to_string(cid()) + " ";
				if(is_lost()) {
					result += "lost";
				} else {
					result += "notifying: {";
					std::size_t remaining = num_notified();
					for(auto& w1 : wait_predecessors()) {
						result += w1->tid().to_string() + "@" + std::to_string(w1->depth()) + (--remaining ? " " : "");
					}
					result += "}";
				}
				if (is_cutoff()) {
					result += " CUTOFF";
				}
				return "[" + result + "]";
			}
			return "broadcast";
		}

		util::iterator_range<event const* const*> predecessors() const noexcept override {
			return util::make_iterator_range<event const* const*>(_predecessors.data(), _predecessors.data() + _predecessors.size());
		}

		event const* thread_predecessor() const noexcept override {
			return _predecessors[0];
		}

		// may return empty range if no wait predecessor exists (broadcast is lost)
		util::iterator_range<event const* const*> wait_predecessors() const noexcept {
			if(!is_lost()) {
				return util::make_iterator_range<event const* const*>(_predecessors.data() + 1, _predecessors.data() + 1 + _num_notified_threads);
			} else {
				return util::make_iterator_range<event const* const*>(nullptr, nullptr);
			}
		}

		// may return empty range if no condition variable predecessor other than condition_variable_create exists
		util::iterator_range<event const* const*> condition_variable_predecessors() const noexcept override {
			return util::make_iterator_range<event const* const*>(_predecessors.data() + 1, _predecessors.data() + _predecessors.size());
		}

		cond_id_t cid() const noexcept override { return _cid; }

		bool is_lost() const noexcept {
			return _num_notified_threads == 0;
		}

		std::size_t num_notified() const noexcept {
			return _num_notified_threads;
		}

		bool is_notifying_thread(thread_id_t tid) const noexcept {
			for(auto& e : wait_predecessors()) {
				if(e->tid() == tid)
					return true;
			}
			return false;
		}
	};
}
