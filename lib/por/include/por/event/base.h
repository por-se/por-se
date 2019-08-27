#pragma once

#include <util/iterator_range.h>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <por/thread_id.h>

#ifdef LIBPOR_IN_KLEE
#include "klee/Fingerprint/MemoryFingerprint.h"
#endif // LIBPOR_IN_KLEE

namespace por {
	class unfolding;
}

namespace por::event {
	using thread_id_t = por::thread_id;
	using lock_id_t = std::uint64_t;
	using cond_id_t = std::uint64_t;

	using path_t = std::vector<bool>;

	enum class event_kind : std::uint8_t {
		local,
		program_init,
		thread_create,
		thread_join,
		thread_init,
		thread_exit,
		lock_create,
		lock_destroy,
		lock_acquire,
		lock_release,
		condition_variable_create,
		condition_variable_destroy,
		wait1,
		wait2,
		signal,
		broadcast,
	};


	class exploration_info {
		mutable std::vector<path_t> open_paths;
		mutable std::vector<path_t> explored_paths;

	public:
		void mark_as_open(path_t const& path) const {
			open_paths.emplace_back(path);
		}

		void mark_as_explored(path_t const& path) const {
			auto it = std::find(open_paths.begin(), open_paths.end(), path);
			if(it != open_paths.end()) {
				explored_paths.emplace_back(std::move(*it));
				open_paths.erase(it);
				return;
			}
			if(!is_explored(path))
				explored_paths.emplace_back(path);
		}

		bool is_explored(path_t const& path) const {
			auto it = std::find(explored_paths.begin(), explored_paths.end(), path);
			return it != explored_paths.end();
		}

		bool is_present(path_t const& path) const {
			auto it = std::find(explored_paths.begin(), explored_paths.end(), path);
			if(it != explored_paths.end())
				return true;
			it = std::find(open_paths.begin(), open_paths.end(), path);
			return it != open_paths.end();
		}
	};

	class event : public std::enable_shared_from_this<event> {
		std::size_t _depth;
		std::map<thread_id_t, event*> _cone;
		thread_id_t _tid;
		const event_kind _kind;

		bool is_explorable() const {
			switch(_kind) {
				case por::event::event_kind::local:
				case por::event::event_kind::lock_acquire:
				case por::event::event_kind::wait1:
				case por::event::event_kind::wait2:
				case por::event::event_kind::signal:
				case por::event::event_kind::broadcast:
					return true;
				default:
					return false;
			}
		}

	public:
#ifdef LIBPOR_IN_KLEE
		klee::MemoryFingerprintValue _fingerprint;
		klee::MemoryFingerprintDelta _thread_delta;
#endif // LIBPOR_IN_KLEE

		event_kind kind() const noexcept { return _kind; }
		thread_id_t tid() const noexcept { return _tid; }
		std::size_t depth() const noexcept { return _depth; }
		std::map<thread_id_t, event*> const& cone() const noexcept { return _cone; }

		virtual void mark_as_open(path_t const& path) const {
			assert(!is_explorable() && "method must be overriden in explorable events!");
		}
		virtual void mark_as_explored(path_t const& path) const {
			assert(!is_explorable() && "method must be overriden in explorable events!");
		}
		virtual bool is_present(path_t const& path) const {
			assert(!is_explorable() && "method must be overriden in explorable events!");
			return true;
		}
		virtual bool is_explored(path_t const& path) const {
			assert(!is_explorable() && "method must be overriden in explorable events!");
			return true;
		}

	protected:
		event(event_kind kind, thread_id_t tid)
		: _depth(0)
		, _tid(tid)
		, _kind(kind)
		{
			// otherwise, depth is wrong
			assert(kind == event_kind::program_init);
		}

		event(event_kind kind, thread_id_t tid, std::shared_ptr<event>& immediate_predecessor)
		: _depth(immediate_predecessor->_depth + 1)
		, _cone(immediate_predecessor->_cone)
		, _tid(tid)
		, _kind(kind)
		{
			// immediate_predecessor may be on different thread than this new event, e.g. in thread_init
			_cone[immediate_predecessor->tid()] = immediate_predecessor.get();

			assert(immediate_predecessor->_depth < _depth);
			assert(_cone.size() >= immediate_predecessor->_cone.size());
		}

	private:
		void insert_predecessor_into_cone(std::shared_ptr<event> const& p) {
			if(!p)
				return;
			for(auto& pred : p->_cone) {
				auto t = pred.first;
				auto& event = pred.second;
				if(_cone.count(t) == 0 || _cone[t]->_depth < event->_depth) {
					_cone[t] = event;
				}
			}

			// p is not part of p->_cone
			if(_cone.count(p->_tid) == 0 || _cone[p->_tid]->_depth < p->_depth) {
				_cone[p->_tid] = p.get();
			}
		}

		event(event_kind kind, thread_id_t tid, std::shared_ptr<event> const& immediate_predecessor, std::shared_ptr<event> const* single_other_predecessor, util::iterator_range<std::shared_ptr<event>*> other_predecessors)
		: _cone(immediate_predecessor->_cone)
		, _tid(tid)
		, _kind(kind)
		{
			_cone[immediate_predecessor->tid()] = immediate_predecessor.get();

			if(single_other_predecessor) {
				insert_predecessor_into_cone(*single_other_predecessor);
			}

			for(auto& op : other_predecessors) {
				insert_predecessor_into_cone(op);
			}

			std::size_t max_depth = 0;
			for(auto& c : _cone) {
				auto& event = c.second;
				max_depth = std::max(max_depth, event->_depth);
			}
			_depth = max_depth + 1;

			assert(immediate_predecessor->_depth < _depth);
			assert(_cone.size() >= immediate_predecessor->_cone.size());
			if(single_other_predecessor != nullptr && *single_other_predecessor != nullptr) {
				assert((*single_other_predecessor)->_depth < _depth);
				assert(_cone.size() >= (*single_other_predecessor)->_cone.size());
			}
			for(auto& op : other_predecessors) {
				if(!op)
					continue;
				assert(op->_depth < _depth);
				assert(_cone.size() >= op->_cone.size());
			}
		}

	protected:
		event(event_kind kind, thread_id_t tid, std::shared_ptr<event> const& immediate_predecessor, std::shared_ptr<event> const& single_other_predecessor, util::iterator_range<std::shared_ptr<event>*> other_predecessors)
		: event(kind, tid, immediate_predecessor, (single_other_predecessor ? &single_other_predecessor : nullptr), other_predecessors)
		{ }

		event(event_kind kind, thread_id_t tid, std::shared_ptr<event> const& immediate_predecessor, util::iterator_range<std::shared_ptr<event>*> other_predecessors)
		: event(kind, tid, immediate_predecessor, nullptr, other_predecessors)
		{ }

		event(event_kind kind, thread_id_t tid, std::shared_ptr<event> const& immediate_predecessor, std::shared_ptr<event> const& single_other_predecessor, std::shared_ptr<event>& yet_another_predecessor)
		: event(kind, tid, immediate_predecessor, &single_other_predecessor, util::make_iterator_range<std::shared_ptr<event>*>(&yet_another_predecessor, &yet_another_predecessor + 1))
		{ }

		event(event_kind kind, thread_id_t tid, std::shared_ptr<event> const& immediate_predecessor, std::shared_ptr<event> const& single_other_predecessor)
		: event(kind, tid, immediate_predecessor, &single_other_predecessor, util::make_iterator_range<std::shared_ptr<event>*>(nullptr, nullptr))
		{ }

		// defined in unfolding.h
		static std::shared_ptr<por::event::event> deduplicate(std::shared_ptr<por::unfolding>& unfolding, std::shared_ptr<por::event::event>&& event);

	public:
		virtual std::string to_string(bool details = false) const = 0;

		virtual util::iterator_range<std::shared_ptr<event>*> predecessors() = 0;
		virtual util::iterator_range<std::shared_ptr<event> const*> predecessors() const = 0;

		bool is_less_than(event const& rhs) const {
			if(rhs._tid == _tid) {
				return _depth < rhs._depth;
			} else {
				auto it = rhs._cone.find(_tid);
				if(it != rhs._cone.end()) {
					event const& g = *it->second;
					return _depth < g._depth || _depth == g._depth;
				}
			}
			return false;
		}

		bool is_less_than_eq(event const& rhs) const {
			return (&rhs == this) || is_less_than(rhs);
		}
	};
}
