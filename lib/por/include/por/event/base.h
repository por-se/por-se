#pragma once

#include <util/iterator_range.h>

#include <cassert>
#include <cstdint>
#include <map>
#include <memory>

namespace por::event {
	using thread_id_t = std::uint32_t;
	using lock_id_t = std::uint64_t;

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

	class event : public std::enable_shared_from_this<event> {
		std::size_t _depth;
		std::map<thread_id_t, event const*> _cone;
		thread_id_t _tid;
		event_kind _kind;


	public:
		event_kind kind() const noexcept { return _kind; }
		thread_id_t tid() const noexcept { return _tid; }
		std::size_t depth() const noexcept { return _depth; }

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
			_cone[tid] = immediate_predecessor.get();

			assert(immediate_predecessor->_depth < _depth);
			assert(_cone.size() >= immediate_predecessor->_cone.size());
		}

		event(event_kind kind, thread_id_t tid, std::shared_ptr<event>& immediate_predecessor, std::shared_ptr<event>& other_predecessor)
		: _cone(immediate_predecessor->_cone)
		, _tid(tid)
		, _kind(kind)
		{
			_cone[tid] = immediate_predecessor.get();

			for(auto& pred : other_predecessor->_cone) {
				auto t = pred.first;
				auto& event = pred.second;
				if(_cone.count(t) == 0 || _cone[t]->_depth < event->_depth) {
					_cone[t] = event;
				}
			}

			// other_predecessor is not part of other_predecessor->_cone
			if(_cone[other_predecessor->_tid]->_depth < other_predecessor->_depth) {
				_cone[other_predecessor->_tid] = other_predecessor.get();
			}

			std::size_t max_depth = 0;
			for(auto& c : _cone) {
				auto& event = c.second;
				max_depth = std::max(max_depth, event->_depth);
			}
			_depth = max_depth + 1;

			assert(immediate_predecessor->_depth < _depth);
			assert(other_predecessor->_depth < _depth);
			assert(_cone.size() >= immediate_predecessor->_cone.size());
			assert(_cone.size() >= other_predecessor->_cone.size());
		}

	public:
		virtual util::iterator_range<std::shared_ptr<event>*> predecessors() = 0;

		bool operator<(event const& rhs) {
			if(rhs._tid == _tid) {
				return _depth < rhs._depth;
			} else {
				auto it = rhs._cone.find(_tid);
				if(it != rhs._cone.end()) {
					event const& g = *it->second;
					return rhs._depth < g._depth && g._depth < _depth;
				}
			}
			return false;
		}
	};
}
