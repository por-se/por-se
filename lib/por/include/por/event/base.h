#pragma once

#include "por/cone.h"
#include "por/thread_id.h"

#include <util/iterator_range.h>

#include <algorithm>
#include <cassert>
#include <cstdint>
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

	class event {
		std::size_t _depth;
		por::cone _cone;
		thread_id_t _tid;
		event_kind _kind;

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
		std::size_t depth() const noexcept { return _depth; }
		auto const& cone() const noexcept { return _cone; }

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

		explicit event() = delete;
		event(const event&) = default;
		event(event&&) = default;
		event& operator=(const event&) = default;
		event& operator=(event&&) = default;
		virtual ~event() = default;

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
			if(single_other_predecessor != nullptr) {
				assert(single_other_predecessor->_depth < _depth);
				assert(_cone.size() >= single_other_predecessor->_cone.size());
			}
			for(auto& op : other_predecessors) {
				if(!op)
					continue;
				assert(op->_depth < _depth);
				assert(_cone.size() >= op->_cone.size());
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

	public:
		virtual std::string to_string(bool details = false) const = 0;

		virtual util::iterator_range<event const* const*> predecessors() const noexcept {
			assert(_kind == event_kind::program_init);
			return util::make_iterator_range<event const* const*>(nullptr, nullptr);
		}

		virtual event const* thread_predecessor() const = 0;

		virtual event const* lock_predecessor() const noexcept {
			return nullptr;
		}

		virtual util::iterator_range<event const* const*> condition_variable_predecessors() const noexcept {
			return util::make_iterator_range<event const* const*>(nullptr, nullptr);
		}

		virtual cond_id_t cid() const noexcept { return 0; }

		bool is_less_than(por::cone const& rhs) const {
			auto it = rhs.find(_tid);
			if(it != rhs.end()) {
				event const& e = *it->second;
				return _depth < e._depth || _depth == e._depth;
			}
			return false;
		}

		bool is_less_than(event const& rhs) const {
			if(rhs._tid == _tid) {
				return _depth < rhs._depth;
			} else {
				return is_less_than(rhs.cone());
			}
			return false;
		}

		bool is_less_than_eq(event const& rhs) const {
			return (&rhs == this) || is_less_than(rhs);
		}
	};
}
