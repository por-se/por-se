#pragma once

#include "por/cone.h"
#include "por/thread_id.h"

#include <util/iterator_range.h>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

#ifdef LIBPOR_IN_KLEE
#include "klee/Fingerprint/MemoryFingerprint.h"
#else
// FIXME: create standalone header in KLEE (this has to match *exactly* for shared lib)
namespace klee {
	using MemoryFingerprintValue = std::array<std::uint8_t, 32>;
	class MemoryFingerprintDelta {
		MemoryFingerprintValue fingerprintValue = {};
		std::unordered_map<const void *, std::uint64_t> symbolicReferences;
	};
}
#endif

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

	class event {
	public:
		using depth_t = std::size_t;
		using color_t = std::size_t;

	private:
		depth_t _depth;
		por::cone _cone; // maximal predecessor per thread (excl. program_init)
		thread_id_t _tid;
		event_kind _kind;

		mutable color_t _color = 0;
		static color_t _next_color;

		mutable std::set<event const*> _successors;

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
		klee::MemoryFingerprintValue _fingerprint;
		klee::MemoryFingerprintDelta _thread_delta;

		event_kind kind() const noexcept { return _kind; }
		thread_id_t const& tid() const noexcept { return _tid; }
		depth_t depth() const noexcept { return _depth; }
		auto const& cone() const noexcept { return _cone; }

		event(event&& that)
		: _depth(that._depth)
		, _cone(std::move(that._cone))
		, _tid(std::move(that._tid))
		, _kind(that._kind)
		, _successors(std::move(that._successors))
		, _fingerprint(std::move(that._fingerprint))
		, _thread_delta(std::move(that._thread_delta)) {
			assert(!that.has_successors());
		}

		virtual ~event() {
			assert(!has_successors());
		}

		event() = delete;
		event(const event&) = delete;
		event& operator=(const event&) = delete;
		event& operator=(event&&) = delete;

	protected:
		event(event_kind kind, thread_id_t tid)
		: _depth(0)
		, _tid(tid)
		, _kind(kind)
		{
			// otherwise, depth is wrong
			assert(kind == event_kind::program_init);
		}

		event(event_kind kind, thread_id_t tid, event const& immediate_predecessor)
		: _depth(immediate_predecessor._depth + 1)
		, _cone(immediate_predecessor)
		, _tid(tid)
		, _kind(kind)
		{
			assert(immediate_predecessor._depth < _depth);
			assert(_cone.size() >= immediate_predecessor._cone.size());
			immediate_predecessor._successors.insert(this);
		}

		event(event_kind kind, thread_id_t tid, event const& immediate_predecessor, event const* single_other_predecessor, util::iterator_range<event const* const*> other_predecessors)
		: _cone(immediate_predecessor, single_other_predecessor, other_predecessors)
		, _tid(tid)
		, _kind(kind)
		{
			std::size_t max_depth = 0;
			for(auto& c : _cone) {
				auto& event = c.second;
				max_depth = std::max(max_depth, event->_depth);
			}
			_depth = max_depth + 1;

			assert(immediate_predecessor._depth < _depth);
			assert(_cone.size() >= immediate_predecessor._cone.size());
			if(&immediate_predecessor == _cone.at(immediate_predecessor.tid())) {
				immediate_predecessor._successors.insert(this);
			}
			if(single_other_predecessor != nullptr) {
				assert(single_other_predecessor->_depth < _depth);
				assert(_cone.size() >= single_other_predecessor->_cone.size());
				if(single_other_predecessor == _cone.at(single_other_predecessor->tid())) {
					single_other_predecessor->_successors.insert(this);
				}
			}
			for(auto& op : other_predecessors) {
				if(!op) {
					continue;
				}
				assert(op->_depth < _depth);
				assert(_cone.size() >= op->_cone.size());
				if(op == _cone.at(op->tid())) {
					op->_successors.insert(this);
				}
			}
		}

		event(event_kind kind, thread_id_t tid, event const& immediate_predecessor, util::iterator_range<event const* const*> other_predecessors)
		: event(kind, tid, immediate_predecessor, nullptr, other_predecessors)
		{ }

		// FIXME: this is ugly
		event(event_kind kind, thread_id_t tid, event const& immediate_predecessor, event const& single_other_predecessor, event const* yet_another_predecessor)
		: event(kind, tid, immediate_predecessor, &single_other_predecessor, util::make_iterator_range<event const* const*>(&yet_another_predecessor, &yet_another_predecessor + 1))
		{ assert(yet_another_predecessor != nullptr); }

		event(event_kind kind, thread_id_t tid, event const& immediate_predecessor, event const* single_other_predecessor)
		: event(kind, tid, immediate_predecessor, single_other_predecessor, util::make_iterator_range<event const* const*>(nullptr, nullptr))
		{ }

		void remove_from_successors_of(event const& event) const noexcept {
			event._successors.erase(this);
		}

		void replace_successor_of(event const& event, por::event::event const& old) const noexcept {
			std::set<por::event::event const*>& succ = event._successors;
			succ.erase(&old);
			succ.insert(this);
		}

		std::set<event const*> local_configuration(color_t color) const noexcept;

		std::set<event const*> causes(color_t color) const noexcept {
			std::size_t orig_color = _color;
			auto W = local_configuration(color);
			W.erase(this);
			_color = orig_color;
			return W;
		}

	public:
		bool has_successors() const noexcept {
			return !_successors.empty();
		}

		virtual std::string to_string(bool details = false) const = 0;

		virtual util::iterator_range<event const* const*> predecessors() const noexcept {
			assert(_kind == event_kind::program_init);
			return util::make_iterator_range<event const* const*>(nullptr, nullptr);
		}

		auto const& successors() const noexcept {
			return _successors;
		}

		virtual event const* thread_predecessor() const = 0;

		virtual event const* lock_predecessor() const noexcept {
			return nullptr;
		}

		virtual util::iterator_range<event const* const*> condition_variable_predecessors() const noexcept {
			return util::make_iterator_range<event const* const*>(nullptr, nullptr);
		}

		std::set<event const*> local_configuration() const noexcept {
			return local_configuration(_next_color++);
		}

		std::set<event const*> causes() const noexcept {
			return causes(_next_color++);
		}

		virtual cond_id_t cid() const noexcept { return 0; }

		bool is_independent_of(event const* other) const noexcept;

		// IMPORTANT: assumes no conflict between this and rhs
		bool is_less_than(por::cone const& rhs) const {
			auto it = rhs.find(_tid);
			if(it != rhs.end()) {
				event const& e = *it->second;
				return _depth < e._depth || _depth == e._depth;
			}
			return false;
		}

		// IMPORTANT: assumes no conflict between this and rhs
		bool is_less_than(event const& rhs) const {
			if(rhs._tid == _tid) {
				return _depth < rhs._depth;
			} else {
				return is_less_than(rhs.cone());
			}
			return false;
		}

		// IMPORTANT: assumes no conflict between this and rhs
		bool is_less_than_eq(event const& rhs) const {
			return (&rhs == this) || is_less_than(rhs);
		}

		std::vector<event const*> immediate_predecessors() const noexcept;

		std::vector<event const*> immediate_conflicts() const noexcept;

		color_t color() const noexcept {
			return _color;
		}

		template<typename T>
		static color_t colorize(T begin, T end) {
			color_t color = ++_next_color;
			for (; begin != end; ++begin) {
				(*begin)->_color = color;
			}
			return color;
		}
	};
}
