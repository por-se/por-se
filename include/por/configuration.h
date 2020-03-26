#pragma once

#include "cone.h"
#include "comb.h"
#include "event/event.h"
#include "unfolding.h"

#include "util/check.h"
#include "util/sso_array.h"

#include <cassert>
#include <iterator>
#include <map>
#include <ostream>
#include <set>
#include <vector>
#include <memory>

namespace por {
	class configuration;

	struct extension {
		std::unique_ptr<por::event::event> event;
		por::configuration const* const configuration;
		std::size_t extension_index;

		por::event::event const* commit(por::configuration& cfg) &&;
	};

	class configuration_iterator {
		por::configuration const* _configuration = nullptr;
		std::map<por::event::thread_id_t, por::event::event const*>::const_reverse_iterator _thread;
		por::event::event const* _event = nullptr;

	public:
		using value_type = por::event::event const*;
		using difference_type = std::ptrdiff_t;
		using pointer = por::event::event const* const*;
		using reference = por::event::event const* const&;

		using iterator_category = std::forward_iterator_tag;

		configuration_iterator() = default;
		explicit configuration_iterator(por::configuration const& configuration, bool end=false);

		reference operator*() const noexcept {
			return _event;
		}
		pointer operator->() const noexcept {
			return &_event;
		}

		configuration_iterator& operator++() noexcept;
		configuration_iterator operator++(int) noexcept {
			configuration_iterator tmp = *this;
			++(*this);
			return tmp;
		}

		bool operator==(const configuration_iterator& rhs) const noexcept {
			libpor_check(decltype(_thread)() == decltype(_thread)());
			return _configuration == rhs._configuration && _thread == rhs._thread && _event == rhs._event;
		}
		bool operator!=(const configuration_iterator& rhs) const noexcept {
			return !(*this == rhs);
		}
	};

	class configuration_root {
		friend class configuration;

		std::shared_ptr<por::unfolding> _unfolding = std::make_shared<por::unfolding>();

		por::event::event const& _program_init = _unfolding->root();

		std::map<por::event::thread_id_t, por::event::event const*> _thread_heads;

	public:
		configuration construct();

		configuration_root& add_thread() {
			event::thread_id_t tid = thread_id(thread_id(), _thread_heads.size() + 1);

			_thread_heads.emplace(tid, _unfolding->deduplicate(event::thread_init::alloc(tid, _program_init)));
			_unfolding->stats_inc_event_created(por::event::event_kind::thread_init);

			[[maybe_unused]] auto init = static_cast<por::event::thread_init const*>(_thread_heads[tid]);
			assert(init->thread_creation_predecessor() == static_cast<por::event::event const*>(&_program_init));

			return *this;
		}
	};

	class configuration {
		friend class node;

		// creation events for locks and condition variables are optional
		static const bool optional_creation_events = true;

		// the unfolding this configuration is part of
		std::shared_ptr<por::unfolding> _unfolding;

		// contains most recent event of ALL threads that ever existed within this configuration
		std::map<por::event::thread_id_t, por::event::event const*> _thread_heads;

		// contains most recent event of ACTIVE locks
		std::map<event::lock_id_t, por::event::event const*> _lock_heads;

		// contains all previous sig, bro events of ACTIVE condition variables for each thread
		std::map<por::event::cond_id_t, std::vector<por::event::event const*>> _cond_heads;

		// contains all previous w2 events of ACTIVE condition variables
		std::map<por::event::cond_id_t, std::vector<por::event::event const*>> _w2_heads;

		// contains all previously used condition variable ids
		std::set<por::event::cond_id_t> _used_cond_ids;

		// contains all previously used lock ids
		std::set<por::event::lock_id_t> _used_lock_ids;

		// number of events in this configuration (excl. catch-up events)
		std::size_t _size = 0;

		// index of last extension (extension can only be applied if number matches)
		mutable std::size_t _last_extension = 0;

		por::extension ex(std::unique_ptr<por::event::event>&& event) const noexcept {
			return {std::move(event), this, ++_last_extension};
		}

	public:
		configuration() : configuration(configuration_root{}.add_thread().construct()) { }
		configuration(configuration const&) = default;
		configuration& operator=(configuration const&) = delete;
		configuration(configuration&&) = default;
		configuration& operator=(configuration&&) = default;
		configuration(configuration_root&& root)
			: _unfolding(std::move(root._unfolding))
			, _thread_heads(std::move(root._thread_heads))
			, _size(_thread_heads.size() + 1)
		{
			_unfolding->stats_inc_event_created(por::event::event_kind::program_init);
			_unfolding->stats_inc_unique_event(por::event::event_kind::program_init);
			assert(!_thread_heads.empty() && "Cannot create a configuration without any startup threads");
		}
		~configuration() = default;

		// iterate over events in configuration (excl. catch-up events)
		configuration_iterator begin() const noexcept { return configuration_iterator(*this); }
		configuration_iterator end() const noexcept { return configuration_iterator(*this, true); }

		auto const& thread_heads() const noexcept { return _thread_heads; }
		auto const& lock_heads() const noexcept { return _lock_heads; }
		auto const& cond_heads() const noexcept { return _cond_heads; }

		por::event::event const* last_of_tid(por::thread_id const& tid) const noexcept {
			auto it = _thread_heads.find(tid);
			if(it == _thread_heads.end()) {
				return nullptr;
			}
			return it->second;
		}

		por::event::event const* last_of_lid(por::event::lock_id_t const& lid) const noexcept {
			auto it = _lock_heads.find(lid);
			if(it == _lock_heads.end()) {
				return nullptr;
			}
			return it->second;
		}

		std::vector<por::event::event const*> last_of_cid(por::event::cond_id_t const& cid) const noexcept {
			auto it = _cond_heads.find(cid);
			if(it == _cond_heads.end()) {
				return {};
			}
			return it->second;
		}

		bool can_acquire_lock(por::event::lock_id_t const& lock) const noexcept {
			assert(lock > 0 && "Lock id must not be zero");
			por::event::event const* lock_event = last_of_lid(lock);
			if constexpr(optional_creation_events) {
				if(!lock_event && _used_lock_ids.count(lock) == 0) {
					return true;
				}
			}

			switch(lock_event->kind()) {
				case por::event::event_kind::lock_create:
				case por::event::event_kind::lock_release:
				case por::event::event_kind::wait1:
					return true;
				default:
					return false;
			}
		}

		bool was_notified(por::thread_id const& thread, por::event::cond_id_t const& cond) const noexcept {
			por::event::event const* wait1 = last_of_tid(thread);
			assert(wait1 != nullptr);
			if(wait1->kind() != por::event::event_kind::wait1) {
				return false;
			}
			return wait2_predecessor_cond(*wait1, last_of_cid(cond)) != nullptr;
		}

		auto const& unfolding() const noexcept { return _unfolding; }

		std::size_t size() const noexcept { return _size; }
		bool empty() const noexcept { return _size == 0; }

		std::size_t active_threads() const noexcept {
			if(_thread_heads.empty())
				return 0;

			std::size_t res = 0;
			for(auto& e : _thread_heads) {
				assert(!!e.second);
				if(e.second->kind() != por::event::event_kind::thread_exit && e.second->kind() != por::event::event_kind::wait1)
					++res;
			}
			return res;
		}

		por::event::event const* commit(por::extension&& ex) {
			if(ex.configuration != this || ex.extension_index != _last_extension) {
				return nullptr;
			}

			por::event::event const* event = _unfolding->deduplicate(std::move(ex.event));

			_thread_heads[event->tid()] = event;
			_unfolding->stats_inc_event_created(event->kind()); // FIXME: increment this somewhere else?
			++_size;

			using por::event::event_kind;
			switch(event->kind()) {
				case event_kind::lock_create: {
					_used_lock_ids.insert(event->lid());
					_lock_heads[event->lid()] = event;
					break;
				}
				case event_kind::lock_acquire: {
					if constexpr(optional_creation_events) {
						if(event->lock_predecessor() == nullptr) {
							_used_lock_ids.insert(event->lid());
						}
					}
					_lock_heads[event->lid()] = event;
					break;
				}
				case event_kind::lock_release: {
					_lock_heads[event->lid()] = event;
					break;
				}
				case event_kind::lock_destroy: {
					if constexpr(optional_creation_events) {
						if(event->lock_predecessor() == nullptr) {
							_used_lock_ids.insert(event->lid());
						} else {
							_lock_heads.erase(event->lid());
						}
					} else {
						_lock_heads.erase(event->lid());
					}
					break;
				}

				case event_kind::condition_variable_create: {
					_used_cond_ids.insert(event->cid());
					_cond_heads.emplace(event->cid(), std::vector{event});
					break;
				}
				case event_kind::wait1: {
					if constexpr(optional_creation_events) {
						if(event->condition_variable_predecessors().empty()) {
							_used_cond_ids.insert(event->cid());
						}
					}
					_lock_heads[event->lid()] = event;
					_cond_heads[event->cid()].push_back(event);
					break;
				}
				case event_kind::wait2: {
					_lock_heads[event->lid()] = event;
					_w2_heads[event->cid()].emplace_back(event);
					break;
				}
				case event_kind::signal: {
					if constexpr(optional_creation_events) {
						if(event->condition_variable_predecessors().empty()) {
							_used_cond_ids.insert(event->cid());
						}
					}
					auto& cond_preds = _cond_heads[event->cid()];
					if(auto sig = static_cast<por::event::signal const*>(event); !sig->is_lost()) {
						auto cond_it = std::find(cond_preds.begin(), cond_preds.end(), sig->wait_predecessor());
						// replace notified wait1 in cond_preds
						*cond_it = event;
					} else {
						cond_preds.push_back(event);
					}
					break;
				}
				case event_kind::broadcast: {
					if constexpr(optional_creation_events) {
						if(event->condition_variable_predecessors().empty()) {
							_used_cond_ids.insert(event->cid());
						}
					}
					auto& cond_preds = _cond_heads[event->cid()];
					if(auto bro = static_cast<por::event::broadcast const*>(event); !bro->is_lost()) {
						auto wait = bro->wait_predecessors();
						cond_preds.erase(std::remove_if(cond_preds.begin(), cond_preds.end(), [&wait](auto* p) {
							return std::find(wait.begin(), wait.end(), p) != wait.end();
						}), cond_preds.end());
					}
					cond_preds.push_back(event);
					break;
				}
				case event_kind::condition_variable_destroy: {
					if constexpr(optional_creation_events) {
						if(event->condition_variable_predecessors().empty()) {
							_used_cond_ids.insert(event->cid());
						} else {
							_cond_heads.erase(event->cid());
							_w2_heads.erase(event->cid());
						}
					} else {
						_cond_heads.erase(event->cid());
						_w2_heads.erase(event->cid());
					}
					break;
				}

				case event_kind::local:
				case event_kind::program_init:
				case event_kind::thread_create:
				case event_kind::thread_exit:
				case event_kind::thread_init:
				case event_kind::thread_join: {
					// do nothing
					break;
				}
			}

			return event;
		}

		void to_dotgraph(std::ostream& os) const noexcept;

		por::extension create_thread(event::thread_id_t thread, event::thread_id_t new_tid) const noexcept {
			auto thread_it = _thread_heads.find(thread);
			assert(thread_it != _thread_heads.end() && "Thread must exist");
			auto& thread_event = thread_it->second;
			assert(thread_event->kind() != por::event::event_kind::thread_exit && "Thread must not yet be exited");
			assert(thread_event->kind() != por::event::event_kind::wait1 && "Thread must not be blocked");

			assert(new_tid);
			assert(thread_heads().find(new_tid) == thread_heads().end() && "Thread with same id already exists");

			return ex(event::thread_create::alloc(thread, *thread_event, new_tid));
		}

		por::extension init_thread(event::thread_id_t thread, event::thread_id_t created_from) const noexcept {
			auto create_it = _thread_heads.find(created_from);
			assert(create_it != _thread_heads.end() && "Creating thread must exist");
			auto& thread_create = create_it->second;
			assert(thread_create->kind() == por::event::event_kind::thread_create && "Creation must happen immediately before");
			assert(thread_create->tid() != thread);

			auto thread_it = _thread_heads.find(thread);
			assert(thread_it == _thread_heads.end() && "Thread must not be initialized");

			return ex(event::thread_init::alloc(thread, *thread_create));
		}

		por::extension join_thread(event::thread_id_t thread, event::thread_id_t joined) const noexcept {
			auto thread_it = _thread_heads.find(thread);
			assert(thread_it != _thread_heads.end() && "Thread must exist");
			auto& thread_event = thread_it->second;
			assert(thread_event->kind() != por::event::event_kind::thread_exit && "Thread must not yet be exited");
			assert(thread_event->kind() != por::event::event_kind::wait1 && "Thread must not be blocked");
			auto joined_it = _thread_heads.find(joined);
			assert(joined_it != _thread_heads.end() && "Joined thread must exist");
			auto& joined_event = joined_it->second;
			assert(joined_event->kind() == por::event::event_kind::thread_exit && "Joined thread must be exited");

			return ex(event::thread_join::alloc(thread, *thread_event, *joined_event));
		}

		por::extension exit_thread(event::thread_id_t thread, bool atomic = false) const noexcept {
			auto thread_it = _thread_heads.find(thread);
			assert(thread_it != _thread_heads.end() && "Thread must exist");
			auto& thread_event = thread_it->second;
			assert(thread_event->kind() != por::event::event_kind::thread_exit && "Thread must not yet be exited");
			assert(thread_event->kind() != por::event::event_kind::wait1 && "Thread must not be blocked");

			assert(active_threads() > 0);

			return ex(event::thread_exit::alloc(thread, *thread_event, atomic));
		}

		por::extension create_lock(event::thread_id_t thread, event::lock_id_t lock) const noexcept {
			auto thread_it = _thread_heads.find(thread);
			assert(thread_it != _thread_heads.end() && "Thread must exist");
			auto& thread_event = thread_it->second;
			assert(thread_event->kind() != por::event::event_kind::thread_exit && "Thread must not yet be exited");
			assert(thread_event->kind() != por::event::event_kind::wait1 && "Thread must not be blocked");

			assert(lock > 0 && "Lock id must not be zero");
			assert(_lock_heads.find(lock) == _lock_heads.end() && "Lock id already taken");
			assert(_used_lock_ids.count(lock) == 0 && "Lock id cannot be reused");

			return ex(event::lock_create::alloc(thread, lock, *thread_event));
		}

		por::extension destroy_lock(event::thread_id_t thread, event::lock_id_t lock) const noexcept {
			auto thread_it = _thread_heads.find(thread);
			assert(thread_it != _thread_heads.end() && "Thread must exist");
			auto& thread_event = thread_it->second;
			assert(thread_event->kind() != por::event::event_kind::thread_exit && "Thread must not yet be exited");
			assert(thread_event->kind() != por::event::event_kind::wait1 && "Thread must not be blocked");
			auto lock_it = _lock_heads.find(lock);
			if constexpr(optional_creation_events) {
				if(_lock_heads.find(lock) == _lock_heads.end()) {
					assert(lock > 0 && "Lock id must not be zero");

					return ex(event::lock_destroy::alloc(thread, lock, *thread_event, nullptr));
				}
			}
			assert(lock_it != _lock_heads.end() && "Lock must (still) exist");
			auto& lock_event = lock_it->second;

			return ex(event::lock_destroy::alloc(thread, lock, *thread_event, lock_event));
		}

		por::extension acquire_lock(event::thread_id_t thread, event::lock_id_t lock) const noexcept {
			auto thread_it = _thread_heads.find(thread);
			assert(thread_it != _thread_heads.end() && "Thread must exist");
			auto& thread_event = thread_it->second;
			assert(thread_event->kind() != por::event::event_kind::thread_exit && "Thread must not yet be exited");
			assert(thread_event->kind() != por::event::event_kind::wait1 && "Thread must not be blocked");
			assert(can_acquire_lock(lock));
			auto lock_it = _lock_heads.find(lock);
			if constexpr(optional_creation_events) {
				if(lock_it == _lock_heads.end()) {
					assert(lock > 0 && "Lock id must not be zero");

					return ex(event::lock_acquire::alloc(thread, lock, *thread_event, nullptr));
				}
			}
			assert(lock_it != _lock_heads.end() && "Lock must (still) exist");
			auto& lock_event = lock_it->second;
			return ex(event::lock_acquire::alloc(thread, lock, *thread_event, lock_event));
		}

		por::extension release_lock(event::thread_id_t thread, event::lock_id_t lock, bool atomic = false) const noexcept {
			auto thread_it = _thread_heads.find(thread);
			assert(thread_it != _thread_heads.end() && "Thread must exist");
			auto& thread_event = thread_it->second;
			assert(thread_event->kind() != por::event::event_kind::thread_exit && "Thread must not yet be exited");
			assert(thread_event->kind() != por::event::event_kind::wait1 && "Thread must not be blocked");
			auto lock_it = _lock_heads.find(lock);
			assert(lock_it != _lock_heads.end() && "Lock must (still) exist");
			auto& lock_event = lock_it->second;

			return ex(event::lock_release::alloc(thread, lock, *thread_event, *lock_event, atomic));
		}

		por::extension create_cond(por::event::thread_id_t thread, por::event::cond_id_t cond) const noexcept {
			auto thread_it = _thread_heads.find(thread);
			assert(thread_it != _thread_heads.end() && "Thread must exist");
			auto& thread_event = thread_it->second;
			assert(thread_event->kind() != por::event::event_kind::thread_exit && "Thread must not yet be exited");
			assert(thread_event->kind() != por::event::event_kind::wait1 && "Thread must not be blocked");
			assert(cond > 0 && "Condition variable id must not be zero");
			assert(_cond_heads.find(cond) == _cond_heads.end() && "Condition variable id already taken");
			assert(_used_cond_ids.count(cond) == 0 && "Condition variable id cannot be reused");

			return ex(por::event::condition_variable_create::alloc(thread, cond, *thread_event));
		}

		por::extension destroy_cond(por::event::thread_id_t thread, por::event::cond_id_t cond) const noexcept {
			auto thread_it = _thread_heads.find(thread);
			assert(thread_it != _thread_heads.end() && "Thread must exist");
			auto& thread_event = thread_it->second;
			assert(thread_event->kind() != por::event::event_kind::thread_exit && "Thread must not yet be exited");
			assert(thread_event->kind() != por::event::event_kind::wait1 && "Thread must not be blocked");
			auto cond_head_it = _cond_heads.find(cond);
			if constexpr(optional_creation_events) {
				assert(cond > 0 && "Condition variable id must not be zero");
				if(cond_head_it == _cond_heads.end()) {
					return ex(por::event::condition_variable_destroy::alloc(thread, cond, *thread_event, {}));
				}
			}
			assert(cond_head_it != _cond_heads.end() && "Condition variable must (still) exist");
			auto& cond_preds = cond_head_it->second;
			assert(cond_preds.size() > 0);

			auto preds = cond_preds;
			if(auto it = _w2_heads.find(cond); it != _w2_heads.end()) {
				preds.insert(preds.end(), it->second.begin(), it->second.end());
			}

			return ex(por::event::condition_variable_destroy::alloc(thread, cond, *thread_event, preds));
		}

	private:
		std::vector<por::event::event const*> wait1_predecessors_cond(
			por::event::event const& thread_event,
			std::vector<por::event::event const*> const& cond_preds
		) const noexcept {
			por::event::thread_id_t thread = thread_event.tid();
			std::vector<por::event::event const*> non_waiting;
			for(auto& pred : cond_preds) {
				if(pred->kind() == por::event::event_kind::wait1)
					continue;

				if(pred->kind() == por::event::event_kind::signal) {
					auto sig = static_cast<por::event::signal const*>(pred);
					if(!sig->is_lost())
						continue;
				}

				if(pred->kind() == por::event::event_kind::broadcast) {
					auto bro = static_cast<por::event::broadcast const*>(pred);
					if(bro->is_notifying_thread(thread))
						continue;
				}

				if(pred->tid() == thread_event.tid())
					continue; // excluded event is in [thread_event]

				if(pred->is_less_than_eq(thread_event))
					continue; // excluded event is in [thread_event]

				non_waiting.push_back(pred);
			}
			return non_waiting;
		}

	public:
		por::extension wait1(por::event::thread_id_t thread, por::event::cond_id_t cond, por::event::lock_id_t lock) const noexcept {
			auto thread_it = _thread_heads.find(thread);
			assert(thread_it != _thread_heads.end() && "Thread must exist");
			auto& thread_event = thread_it->second;
			assert(thread_event->kind() != por::event::event_kind::thread_exit && "Thread must not yet be exited");
			assert(thread_event->kind() != por::event::event_kind::wait1 && "Thread must not be blocked");
			auto cond_head_it = _cond_heads.find(cond);
			auto lock_it = _lock_heads.find(lock);
			if constexpr(optional_creation_events) {
				if(cond_head_it == _cond_heads.end()) {
					assert(cond > 0 && "Condition variable id must not be zero");
					assert(lock > 0 && "Lock id must not be zero");
					assert(lock_it != _lock_heads.end() && "Lock must (still) exist");
					auto& lock_event = lock_it->second;

					return ex(por::event::wait1::alloc(thread, cond, lock, *thread_event, *lock_event, {}));
				}
			}

			assert(cond_head_it != _cond_heads.end() && "Condition variable must (still) exist");
			auto& cond_preds = cond_head_it->second;
			assert(lock_it != _lock_heads.end() && "Lock must (still) exist");
			auto& lock_event = lock_it->second;

			std::vector<por::event::event const*> non_waiting = wait1_predecessors_cond(*thread_event, cond_preds);
			return ex(por::event::wait1::alloc(thread, cond, lock, *thread_event, *lock_event, std::move(non_waiting)));
		}

	private:
		por::event::event const* wait2_predecessor_cond(
			por::event::event const& wait1,
			std::vector<por::event::event const*> const& cond_preds
		) const noexcept {
			for(auto& e : cond_preds) {
				if(e->kind() == por::event::event_kind::broadcast) {
					auto bro = static_cast<por::event::broadcast const*>(e);
					for(auto& w1 : bro->wait_predecessors()) {
						if(w1 == &wait1)
							return e;
					}
				} else if(e->kind() == por::event::event_kind::signal) {
					auto sig = static_cast<por::event::signal const*>(e);
					if(sig->wait_predecessor() == &wait1)
						return e;
				}
			}
			return nullptr;
		}

	public:
		por::extension wait2(por::event::thread_id_t thread, por::event::cond_id_t cond, por::event::lock_id_t lock) const noexcept {
			auto thread_it = _thread_heads.find(thread);
			assert(thread_it != _thread_heads.end() && "Thread must exist");
			auto& thread_event = thread_it->second;
			assert(thread_event->kind() != por::event::event_kind::thread_exit && "Thread must not yet be exited");
			assert(thread_event->kind() == por::event::event_kind::wait1 && "Thread must be waiting");
			auto cond_head_it = _cond_heads.find(cond);
			assert(cond_head_it != _cond_heads.end() && "Condition variable must (still) exist");
			auto& cond_preds = cond_head_it->second;
			auto lock_it = _lock_heads.find(lock);
			assert(lock_it != _lock_heads.end() && "Lock must (still) exist");
			auto& lock_event = lock_it->second;

			auto cond_event = wait2_predecessor_cond(*thread_event, cond_preds);
			assert(cond_event && "There has to be a notifying event before a wait2");

			return ex(por::event::wait2::alloc(thread, cond, lock, *thread_event, *lock_event, *cond_event));
		}

	private:
		auto notified_wait1_predecessor(
			por::event::thread_id_t notified_thread,
			std::vector<por::event::event const*> const& cond_preds
		) const noexcept {
			auto cond_it = std::find_if(cond_preds.begin(), cond_preds.end(), [&notified_thread](auto& e) {
				return e->tid() == notified_thread && e->kind() == por::event::event_kind::wait1;
			});
			assert(cond_it != cond_preds.end() && "Wait1 event must be in cond_heads");
			return cond_it;
		}

		// extracts non-lost notification events not included in [thread_event]
		// where thread_event is the same-thread predecessor of a signal or broadcast to be created
		std::vector<por::event::event const*> lost_notification_predecessors_cond(
			por::event::event const& thread_event,
			std::vector<por::event::event const*> const& cond_preds
		) const noexcept {
			std::vector<por::event::event const*> prev_notifications;
			for(auto& pred : cond_preds) {
				if(pred->kind() == por::event::event_kind::wait1) {
					assert(0 && "signal or broadcast would not have been lost");
				} else if(pred->kind() == por::event::event_kind::broadcast) {
					auto bro = static_cast<por::event::broadcast const*>(pred);
					if(bro->is_lost())
						continue;

					if(bro->is_notifying_thread(thread_event.tid()))
						continue; // excluded event is in [thread_event]
				} else if(pred->kind() == por::event::event_kind::signal) {
					auto sig = static_cast<por::event::signal const*>(pred);
					if(sig->is_lost())
						continue;

					if(sig->notified_thread() == thread_event.tid())
						continue; // excluded event is in [thread_event]
				}

				if(pred->tid() == thread_event.tid())
					continue; // excluded event is in [thread_event]

				if(pred->is_less_than_eq(thread_event))
					continue; // excluded event is in [thread_event]

				prev_notifications.push_back(pred);
			}
			return prev_notifications;
		}

	public:
		por::extension
		signal_thread(por::event::thread_id_t thread, por::event::cond_id_t cond, por::event::thread_id_t notified_thread) const noexcept {
			auto thread_it = _thread_heads.find(thread);
			assert(thread_it != _thread_heads.end() && "Thread must exist");
			auto& thread_event = thread_it->second;
			assert(thread_event->kind() != por::event::event_kind::thread_exit && "Thread must not yet be exited");
			assert(thread_event->kind() != por::event::event_kind::wait1 && "Thread must not be blocked");
			auto cond_head_it = _cond_heads.find(cond);
			if constexpr(optional_creation_events) {
				if(cond_head_it == _cond_heads.end() && !notified_thread) {
					// only possible for lost signal: otherwise there would be at least a wait1 in _cond_heads
					assert(cond > 0 && "Condition variable id must not be zero");

					return ex(por::event::signal::alloc(thread, cond, *thread_event, std::vector<por::event::event const*>()));
				}
			}
			assert(cond_head_it != _cond_heads.end() && "Condition variable must (still) exist");
			auto& cond_preds = cond_head_it->second;

			if(!notified_thread) { // lost signal
				auto prev_notifications = lost_notification_predecessors_cond(*thread_event, cond_preds);

				return ex(por::event::signal::alloc(thread, cond, *thread_event, std::move(prev_notifications)));
			} else { // notifying signal
				assert(notified_thread != thread && "Thread cannot notify itself");
				auto notified_thread_it = _thread_heads.find(notified_thread);
				assert(notified_thread_it != _thread_heads.end() && "Notified thread must exist");
				auto& notified_thread_event = notified_thread_it->second;
				assert(notified_thread_event->kind() != por::event::event_kind::thread_exit && "Thread must not yet be exited");
				assert(notified_thread_event->kind() == por::event::event_kind::wait1 && "Notified thread must be waiting");

				auto& cond_event = *notified_wait1_predecessor(notified_thread, cond_preds);
				assert(cond_event == notified_thread_event);

				return ex(por::event::signal::alloc(thread, cond, *thread_event, *cond_event));
			}
		}

	public:
		por::extension
		broadcast_threads(por::event::thread_id_t thread, por::event::cond_id_t cond, std::vector<por::event::thread_id_t> notified_threads) const noexcept {
			auto thread_it = _thread_heads.find(thread);
			assert(thread_it != _thread_heads.end() && "Thread must exist");
			auto& thread_event = thread_it->second;
			assert(thread_event->kind() != por::event::event_kind::thread_exit && "Thread must not yet be exited");
			assert(thread_event->kind() != por::event::event_kind::wait1 && "Thread must not be blocked");
			auto cond_head_it = _cond_heads.find(cond);
			if constexpr(optional_creation_events) {
				if(cond_head_it == _cond_heads.end() && notified_threads.empty()) {
					// only possible for lost broadcast: otherwise there would be at least a wait1 in _cond_heads
					assert(cond > 0 && "Condition variable id must not be zero");

					return ex(por::event::broadcast::alloc(thread, cond, *thread_event, {}));
				}
			}
			assert(cond_head_it != _cond_heads.end() && "Condition variable must (still) exist");
			auto& cond_preds = cond_head_it->second;

			if(notified_threads.empty()) { // lost broadcast
				auto prev_notifications = lost_notification_predecessors_cond(*thread_event, cond_preds);

				return ex(por::event::broadcast::alloc(thread, cond, *thread_event, std::move(prev_notifications)));
			} else { // notifying broadcast
				std::vector<por::event::event const*> prev_events;
				for(auto& nid : notified_threads) {
					assert(nid != thread && "Thread cannot notify itself");
					auto notified_thread_it = _thread_heads.find(nid);
					assert(notified_thread_it != _thread_heads.end() && "Notified thread must exist");
					auto& notified_thread_event = notified_thread_it->second;
					assert(notified_thread_event->kind() != por::event::event_kind::thread_exit && "Thread must not yet be exited");
					assert(notified_thread_event->kind() == por::event::event_kind::wait1 && "Notified thread must be waiting");

					auto cond_it = notified_wait1_predecessor(nid, cond_preds);
					auto& cond_event = *cond_it;
					assert(cond_event == notified_thread_event);

					prev_events.push_back(notified_thread_event);
				}

				for(auto& pred : cond_preds) {
					if(pred->kind() == por::event::event_kind::wait1)
						continue; // relevant wait1s already part of prev_events

					if(pred->kind() == por::event::event_kind::condition_variable_create)
						continue; // excluded event is included in wait1's causes (if it exists)

					if(pred->kind() == por::event::event_kind::broadcast)
						continue;

					if(pred->kind() == por::event::event_kind::signal) {
						auto sig = static_cast<por::event::signal const*>(pred);
						if(sig->is_lost())
							continue;

						if(sig->notified_thread() == thread)
							continue; // excluded event is in [thread_event]

						if(std::find(notified_threads.begin(), notified_threads.end(), sig->notified_thread()) != notified_threads.end())
							continue;
					}

					if(pred->tid() == thread)
						continue; // excluded event is in [thread_event]

					if(pred->is_less_than_eq(*thread_event))
						continue; // excluded event is in [thread_event]

					prev_events.push_back(pred);
				}

				return ex(por::event::broadcast::alloc(thread, cond, *thread_event, std::move(prev_events)));
			}
		}

		template<typename D>
		por::extension local(event::thread_id_t thread, std::vector<D> local_path) const noexcept {
			auto thread_it = _thread_heads.find(thread);
			assert(thread_it != _thread_heads.end() && "Thread must exist");
			auto& thread_event = thread_it->second;
			assert(thread_event->kind() != por::event::event_kind::thread_exit && "Thread must not yet be exited");

			return ex(event::local<D>::alloc(thread, *thread_event, std::move(local_path)));
		}

	private:
		std::vector<por::unfolding::deduplication_result> cex_acquire(por::event::event const& e) const noexcept {
			assert(e.kind() == por::event::event_kind::lock_acquire || e.kind() == por::event::event_kind::wait2);

			std::vector<por::unfolding::deduplication_result> result;

			// immediate causal predecessor on same thread
			por::event::event const* et = e.thread_predecessor();
			// maximal event concerning same lock in history of e
			por::event::event const* er = e.lock_predecessor();
			// maximal event concerning same lock in [et] (acq) or [et] \cup [es] (wait2)
			por::event::event const* em = er;
			// signaling event (only for wait2)
			por::event::event const* es = nullptr;

			if(et->is_cutoff()) {
				return {};
			}

			assert(et != nullptr);

			if(e.kind() == por::event::event_kind::lock_acquire) {
				while(em != nullptr && !em->is_less_than_eq(*et)) {
					// descend chain of lock events until em is in [et]
					em = em->lock_predecessor();
				}
			} else {
				assert(e.kind() == por::event::event_kind::wait2);
				es = static_cast<por::event::wait2 const*>(&e)->notifying_predecessor();
				assert(es != nullptr);

				if(es->is_cutoff()) {
					return {};
				}

				while(em != nullptr && !em->is_less_than_eq(*et) && !em->is_less_than(*es)) {
					// descend chain of lock events until em is in [et] \cup [es]
					em = em->lock_predecessor();
				}
			}

			if(em == er) {
				return {};
			}

			if(em == nullptr) {
				assert(e.kind() == por::event::event_kind::lock_acquire); // wait2 must have a wait1 or release as predecessor
				result.emplace_back(_unfolding->deduplicate(por::event::lock_acquire::alloc(e.tid(), e.lid(), *et, nullptr)));
				_unfolding->stats_inc_event_created(por::event::event_kind::lock_acquire);
			} else if(em->kind() == por::event::event_kind::lock_release || em->kind() == por::event::event_kind::wait1) {
				if(e.kind() == por::event::event_kind::lock_acquire) {
					result.emplace_back(_unfolding->deduplicate(por::event::lock_acquire::alloc(e.tid(), e.lid(), *et, em)));
					_unfolding->stats_inc_event_created(por::event::event_kind::lock_acquire);
				} else if(em->kind() == por::event::event_kind::lock_release) {
					assert(e.kind() == por::event::event_kind::wait2);
					result.emplace_back(_unfolding->deduplicate(por::event::wait2::alloc(e.tid(), e.cid(), e.lid(), *et, *em, *es)));
					_unfolding->stats_inc_event_created(por::event::event_kind::wait2);
				}
			} else if(em->kind() == por::event::event_kind::lock_create) {
				assert(e.kind() == por::event::event_kind::lock_acquire); // wait2 must have a wait1 or release as predecessor
				result.emplace_back(_unfolding->deduplicate(por::event::lock_acquire::alloc(e.tid(), e.lid(), *et, em)));
				_unfolding->stats_inc_event_created(por::event::event_kind::lock_acquire);
			}

			assert(er != nullptr); // if er is nullptr, em == er, so we already returned
			por::event::event const* ep = er->lock_predecessor(); // lock events in K \ {r}
			while(ep != nullptr && (em == nullptr || !ep->is_less_than_eq(*em)) && (es == nullptr || !ep->is_less_than_eq(*es))) {
				if(ep->kind() == por::event::event_kind::lock_release || ep->kind() == por::event::event_kind::wait1 || ep->kind() == por::event::event_kind::lock_create) {
					if(e.kind() == por::event::event_kind::lock_acquire) {
						result.emplace_back(_unfolding->deduplicate(por::event::lock_acquire::alloc(e.tid(), e.lid(), *et, ep)));
						_unfolding->stats_inc_event_created(por::event::event_kind::lock_acquire);
					} else {
						assert(e.kind() == por::event::event_kind::wait2);
						assert(ep->kind() != por::event::event_kind::lock_create);
						result.emplace_back(_unfolding->deduplicate(por::event::wait2::alloc(e.tid(), e.cid(), e.lid(), *et, *ep, *es)));
						_unfolding->stats_inc_event_created(por::event::event_kind::wait2);
					}
				}
				ep = ep->lock_predecessor();
			}

			return result;
		}

		std::vector<por::unfolding::deduplication_result> cex_wait1(por::event::event const& e) const noexcept {
			assert(e.kind() == por::event::event_kind::wait1);

			std::vector<por::unfolding::deduplication_result> result;

			// immediate causal predecessor on same thread
			por::event::event const* et = e.thread_predecessor();

			if(et->is_cutoff()) {
				return {};
			}

			// exclude condition variable create event from comb (if present)
			por::event::event const* cond_create = nullptr;

			por::comb comb;
			for(auto& p : e.condition_variable_predecessors()) {
				// cond predecessors contains exactly the bro and lost sig causes outside of [et] plus cond_create (if exists)
				if(p->kind() == por::event::event_kind::condition_variable_create) {
					cond_create = p;
				} else {
					assert(p->tid() != e.tid() && !p->is_less_than(*et));
					comb.insert(*p);
				}
			}

			comb.concurrent_combinations([&](auto const& M) {
				bool cex_found = false;

				por::cone cone(*et, cond_create, util::make_iterator_range<por::event::event const* const*>(M.data(), M.data() + M.size()));

				// check if [M] \cup [et] != [e] \setminus {e}
				// NOTE: lock predecessor is an event on the same thread
				assert(cone.size() <= e.cone().size());
				if(cone.size() != e.cone().size()) {
					cex_found = true;
				} else {
					for(auto& [tid, c] : e.cone()) {
						if(cone.at(tid)->is_less_than(*c)) {
							cex_found = true;
							break;
						}
					}
				}

				if(!cex_found)
					return false;

				// lock predecessor is guaranteed to be in [et] (has to be an aquire/wait2 on the same thread)
				std::vector<por::event::event const*> N(M);
				if(cond_create) {
					N.push_back(cond_create);
				}
				result.emplace_back(_unfolding->deduplicate(por::event::wait1::alloc(e.tid(), e.cid(), e.lid(), *et, *e.lock_predecessor(), std::move(N))));
				_unfolding->stats_inc_event_created(por::event::event_kind::wait1);
				return false; // result of concurrent_combinations not needed
			});

			return result;
		}

		static std::vector<por::event::event const*> outstanding_wait1(por::event::cond_id_t cid, por::cone const& cone) noexcept {
			// collect threads blocked on wait1 on cond cid
			std::vector<por::event::event const*> wait1s;
			for(auto& [tid, c] : cone) {
				if(c->kind() == por::event::event_kind::wait1) {
					if(c->cid() == cid)
						wait1s.emplace_back(c);
				}
			}

			if(wait1s.empty())
				return wait1s;

			// sort wait1 events s.t. the first element always has the minimum depth
			std::sort(wait1s.begin(), wait1s.end(), [](auto& a, auto& b) {
				return a->depth() < b->depth();
			});

			// remove those that have already been notified (so just their w2 event is missing in cone)
			for(auto& [tid, c] : cone) {
				for(auto const* e = c; e != nullptr; e = e->thread_predecessor()) {
					if(wait1s.empty())
						break; // no wait1s left

					if(e->depth() < wait1s.front()->depth())
						break; // none of the waits can be notified by a predecessor on this thread

					if(e->kind() == por::event::event_kind::signal) {
						auto const* sig = static_cast<por::event::signal const*>(e);
						if(sig->cid() != cid)
							continue;

						if(sig->is_lost())
							continue;

						auto wait = sig->wait_predecessor();
						wait1s.erase(std::remove(wait1s.begin(), wait1s.end(), wait), wait1s.end());
					} else if(e->kind() == por::event::event_kind::broadcast) {
						auto const* bro = static_cast<por::event::broadcast const*>(e);
						if(bro->cid() != cid)
							continue;

						if(bro->is_lost())
							continue;

						wait1s.erase(std::remove_if(wait1s.begin(), wait1s.end(), [&](auto& w) {
							for(auto& n : bro->wait_predecessors()) {
								if(n->tid() != w->tid())
									continue;

								if(n->depth() == w->depth())
									return true;
							}
							return false;
						}), wait1s.end());
					}
				}
			}

			return wait1s;
		}

		static std::vector<por::event::event const*> outstanding_wait1(por::event::cond_id_t cid, std::vector<por::event::event const*> events) noexcept {
			assert(!events.empty());
			if(events.size() == 1) {
				assert(events.front() != nullptr);
				return outstanding_wait1(cid, events.front()->cone());
			}

			por::cone cone(util::make_iterator_range<por::event::event const* const*>(events.data(), events.data() + events.size()));
			return outstanding_wait1(cid, cone);
		}

		std::vector<por::unfolding::deduplication_result> cex_notification(por::event::event const& e) const noexcept {
			assert(e.kind() == por::event::event_kind::signal || e.kind() == por::event::event_kind::broadcast);

			std::vector<por::unfolding::deduplication_result> result;

			// immediate causal predecessor on same thread
			por::event::event const* et = e.thread_predecessor();

			if(et->is_cutoff()) {
				return {};
			}

			// exclude condition variable create event from comb (if present)
			por::event::event const* cond_create = nullptr;

			// condition variable id
			por::event::cond_id_t cid = e.cid();

			// calculate maximal event(s) in causes outside of [et]
			std::vector<por::event::event const*> max;
			{
				// compute comb
				por::comb comb;
				for(auto& p : e.condition_variable_predecessors()) {
					// cond predecessors contain all causes outside of [et]
					if(p->tid() == e.tid() || p->is_less_than(*et))
						continue; // cond_create and wait1s (because of their lock relation) can be in in [et]
					comb.insert(*p);
				}
				max = comb.max();
			}

			// calculate comb, containing all w1, sig, bro events on same cond outside of [et] \cup succ(e) (which contains e)
			// we cannot use cond predecessors here as these are not complete
			por::comb comb;
			// also prepare wait1_comb (version of comb with only wait1 events on same cond)
			por::comb wait1_comb;
			for(auto& thread_head : _thread_heads) {
				por::event::event const* pred = thread_head.second;
				do {
					if(pred->tid() == e.tid())
						break; // all events on this thread are either in [et] or succ(e)

					if(e.is_less_than(*pred))
						break; // pred and all its predecessors are in succ(e)

					if(pred->is_less_than(*et))
						break; // pred and all its predecessors are in [et]

					if(pred->cid() == cid) {
						// only include events on same cond
						if(pred->kind() == por::event::event_kind::condition_variable_create) {
							cond_create = pred; // exclude from comb
						} else if(pred->kind() != por::event::event_kind::wait2) {
							// also exclude wait2 events
							comb.insert(*pred);
							if(pred->kind() == por::event::event_kind::wait1) {
								wait1_comb.insert(*pred);
							}
						}
					}

					pred = pred->thread_predecessor();
				} while(pred != nullptr);
			}

			// conflicting extensions: lost notification events
			comb.concurrent_combinations([&](auto const& M) {
				// ensure that M differs from max
				if(max.size() == M.size()) {
					bool ismax = true;
					for(auto& m : M) {
						for(auto& x : max) {
							if(m->is_less_than(*x)) {
								// at least one element of M is smaller than some element of max
								ismax = false;
								break;
							}
						}
					}
					if(ismax)
						return false;
				}

				// ensure that there are only non-lost notifications in M
				if(M.size() == 1 && M.front()->kind() == por::event::event_kind::broadcast) {
					auto* bro = static_cast<por::event::broadcast const*>(M.front());
					if(bro->is_lost())
						return false;
				} else {
					for(auto& m : M) {
						if(m->kind() != por::event::event_kind::signal)
							return false;
						auto* sig = static_cast<por::event::signal const*>(m);
						if(sig->is_lost())
							return false;
					}
				}

				// ensure that there are no outstanding wait1s on same condition variable
				std::vector<por::event::event const*> M_et(M.begin(), M.end());
				M_et.reserve(M.size() + 1);
				M_et.push_back(et);

				if(!outstanding_wait1(cid, M_et).empty())
					return false;

				// create set of cond predecessors
				std::vector<por::event::event const*> N;
				for(auto& m : M) {
					// NOTE: M already contains only events that are not in [et]
					if(m->kind() == por::event::event_kind::broadcast) {
						auto bro = static_cast<por::event::broadcast const*>(m);
						if(bro->is_lost())
							continue;

						if(bro->is_notifying_thread(e.tid()))
							continue;
					} else if(m->kind() == por::event::event_kind::signal) {
						auto sig = static_cast<por::event::signal const*>(m);
						if(sig->is_lost())
							continue;

						if(sig->notified_thread() == e.tid())
							continue;
					} else {
						continue; // exclude all other events
					}
					N.push_back(m);
				}
				if(cond_create) {
					N.push_back(cond_create);
				}

				if(e.kind() == por::event::event_kind::signal) {
					result.emplace_back(_unfolding->deduplicate(por::event::signal::alloc(e.tid(), cid, *et, std::move(N))));
					_unfolding->stats_inc_event_created(por::event::event_kind::signal);
				} else if(e.kind() == por::event::event_kind::broadcast) {
					result.emplace_back(_unfolding->deduplicate(por::event::broadcast::alloc(e.tid(), cid, *et, std::move(N))));
					_unfolding->stats_inc_event_created(por::event::event_kind::broadcast);
				}

				return false; // result of concurrent_combinations not needed
			});

			// conflicting extensions: signal events
			if(e.kind() == por::event::event_kind::signal) {
				auto sig = static_cast<por::event::signal const*>(&e);

				// generate set W with all wait1 events in comb and those that are outstanding in [et]
				std::vector<por::event::event const*> W = outstanding_wait1(cid, et->cone());
				for(auto& [tid, tooth] : wait1_comb.threads()) {
					W.reserve(W.size() + tooth.size());
					for(auto& e : tooth) {
						W.push_back(e);
					}
				}

				// compute map from each w1 to its (non-lost) notification in comb
				std::map<por::event::event const*, por::event::event const*> notification;
				for(auto& [tid, tooth] : comb.threads()) {
					for(auto& n : tooth) {
						if(n->kind() != por::event::event_kind::signal && n->kind() != por::event::event_kind::broadcast)
							continue;

						if(n->kind() == por::event::event_kind::signal) {
							auto sig = static_cast<por::event::signal const*>(n);
							if(sig->is_lost())
								continue;

							notification.emplace(sig->wait_predecessor(), n);
						}

						if(n->kind() == por::event::event_kind::broadcast) {
							auto bro = static_cast<por::event::broadcast const*>(n);
							if(bro->is_lost())
								continue;

							for(auto& w : bro->wait_predecessors()) {
								notification.emplace(w, n);
							}
						}
					}
				}

				for(auto& w : W) {
					if(w == sig->wait_predecessor())
						continue;

					result.emplace_back(_unfolding->deduplicate(por::event::signal::alloc(e.tid(), cid, *et, *w)));
					_unfolding->stats_inc_event_created(por::event::event_kind::signal);
				}
			}

			// conflicting extensions: broadcast events
			if(e.kind() == por::event::event_kind::broadcast) {
				comb.concurrent_combinations([&](auto const& M) {
					// ensure that M differs from max
					if(max.size() == M.size()) {
						bool ismax = true;
						for(auto& m : M) {
							for(auto& x : max) {
								if(m->is_less_than(*x)) {
									// at least one element of M is smaller than some element of max
									ismax = false;
									break;
								}
							}
						}
						if(ismax)
							return false;
					}

					// ensure that M only contains non-lost signal and wait1 events
					for(auto& m : M) {
						if(m->kind() == por::event::event_kind::wait1)
							continue;

						if(m->kind() == por::event::event_kind::signal) {
							auto* sig = static_cast<por::event::signal const*>(m);
							if(sig->is_lost())
								return false;
						}

						return false;
					}

					// ensure that there are outstanding wait1s on same condition variable
					std::vector<por::event::event const*> M_et(M.begin(), M.end());
					M_et.reserve(M.size() + 1);
					M_et.push_back(et);

					auto outstanding = outstanding_wait1(cid, M_et);
					if(outstanding.empty())
						return false;

					// create set of cond predecessors (which is exactly M)
					// because here, it is guaranteed that contained signals do not notify any of the wait1s
					std::vector<por::event::event const*> N;
					N.reserve(M.size());
					for(auto& m : M) {
						N.push_back(m);
					}

					result.emplace_back(_unfolding->deduplicate(por::event::broadcast::alloc(e.tid(), cid, *et, N)));
					_unfolding->stats_inc_event_created(por::event::event_kind::broadcast);
					return false; // result of concurrent_combinations not needed
				});
			}

			return result;
		}

	public:
		std::vector<por::event::event const*>
		conflicting_extensions_deadlock(por::thread_id tid,
		                                por::event::lock_id_t lid,
		                                por::event::event_kind kind,
		                                bool unknown_only = false) const noexcept {
			por::event::event const* et = last_of_tid(tid);

			if(et->is_cutoff()) {
				return {};
			}

			por::event::event const* em = last_of_lid(lid);
			por::event::event const* es = nullptr;

			assert(em);

			// P = [et]
			por::cone P(*et);

			if(kind == por::event::event_kind::wait2) {
				assert(et->kind() == por::event::event_kind::wait1);
				assert(et->has_successors());

				auto cond_it = _cond_heads.find(et->cid());
				assert(cond_it != _cond_heads.end());
				auto& cond_preds = cond_it->second;
				for(auto& p : cond_preds) {
					if(p->kind() == por::event::event_kind::broadcast) {
						auto bro = static_cast<por::event::broadcast const*>(p);
						for(auto& w1 : bro->wait_predecessors()) {
							if(w1 == et) {
								es = p;
								break;
							}
						}
						if(es) {
							break;
						}
					} else if(p->kind() == por::event::event_kind::signal) {
						auto sig = static_cast<por::event::signal const*>(p);
						if(sig->wait_predecessor() == et) {
							es = p;
							break;
						}
					}
				}

				if(es == nullptr) {
					return {};
				} else if(es->is_cutoff()) {
					return {};
				}

				// P = [et] \cup [es]
				P.insert(*es);
			} else {
				assert(kind == por::event::event_kind::lock_acquire);
			}

			por::cone C(*this);
			por::comb A = C.setminus(P);
			A.insert(*em);
			por::comb X(A, [&lid](por::event::event const& e) {
				return e.lid() == lid
				       && !e.is_cutoff()
				       && (e.kind() == por::event::event_kind::lock_release
				           || e.kind() == por::event::event_kind::wait1
				           || e.kind() == por::event::event_kind::lock_create);
			});

			std::vector<por::unfolding::deduplication_result> candidates;
			for(auto& em : X) {
				if(em->is_cutoff()) {
					continue;
				}
				if(kind == por::event::event_kind::lock_acquire) {
					candidates.emplace_back(_unfolding->deduplicate(por::event::lock_acquire::alloc(tid, lid, *et, em)));
					_unfolding->stats_inc_event_created(por::event::event_kind::lock_acquire);
				} else {
					assert(kind == por::event::event_kind::wait2);
					assert(em->kind() != por::event::event_kind::lock_create);
					candidates.emplace_back(_unfolding->deduplicate(por::event::wait2::alloc(tid, es->cid(), lid, *et, *em, *es)));
					_unfolding->stats_inc_event_created(por::event::event_kind::wait2);
				}
			}

			std::vector<por::event::event const*> result;
			std::for_each(candidates.begin(), candidates.end(), [&unknown_only, &result](auto const& dedup) {
				if(unknown_only && !dedup.unknown) {
					return;
				}
				libpor_check(!dedup.event.is_cutoff());
				result.emplace_back(dedup);
			});
			_unfolding->stats_inc_cex_created(result.size());
			return result;
		}

		std::vector<por::event::event const*> conflicting_extensions(bool unknown_only = false) const noexcept {
			_unfolding->stats_inc_configuration();
			std::vector<por::event::event const*> result;
			for(auto* e : *this) {
				std::vector<por::unfolding::deduplication_result> candidates;
				switch(e->kind()) {
					case por::event::event_kind::lock_acquire: {
						auto acq = static_cast<por::event::lock_acquire const*>(e);
						if(acq->all_cex_known()) {
							continue;
						}
						candidates = cex_acquire(*e);
						acq->mark_all_cex_known();
						break;
					}
					case por::event::event_kind::wait2: {
						auto w2 = static_cast<por::event::wait2 const*>(e);
						if(w2->all_cex_known()) {
							continue;
						}
						candidates = cex_acquire(*e);
						w2->mark_all_cex_known();
						break;
					}
					case por::event::event_kind::wait1:
						candidates = cex_wait1(*e);
						break;
					case por::event::event_kind::signal:
					case por::event::event_kind::broadcast:
						candidates = cex_notification(*e);
						break;
					default:
						continue;
				}
				std::for_each(candidates.begin(), candidates.end(), [&unknown_only, &result](auto const& dedup) {
					if((unknown_only && !dedup.unknown) || dedup.event.is_cutoff()) {
						return;
					}
					result.emplace_back(dedup);
				});
			}
			_unfolding->stats_inc_cex_created(result.size());
			return result;
		}

		por::event::event const* compute_alternative(std::vector<por::event::event const*> D) const noexcept {
			return unfolding()->compute_alternative(*this, D);
		}
	};

	inline configuration configuration_root::construct() { return configuration(std::move(*this)); }
}
