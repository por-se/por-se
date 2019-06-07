#pragma once

#include <util/iterator_range.h>

#include <cassert>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace por::event {
	using thread_id_t = std::uint32_t;
	using lock_id_t = std::uint64_t;
	using cond_id_t = std::uint64_t;

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
		std::map<thread_id_t, event*> _cone;
		thread_id_t _tid;
		event_kind _kind;

	public:
		mutable bool visited = false;
		mutable std::vector<std::vector<bool>> visited_paths;

		event_kind kind() const noexcept { return _kind; }
		thread_id_t tid() const noexcept { return _tid; }
		std::size_t depth() const noexcept { return _depth; }
		std::map<thread_id_t, event*> const& cone() const noexcept { return _cone; }

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
			if(_cone[p->_tid]->_depth < p->_depth) {
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
	};
}
