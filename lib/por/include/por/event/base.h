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

namespace por::event {
	using thread_id_t = por::thread_id;
	using lock_id_t = std::uint64_t;
	using cond_id_t = std::uint64_t;

	using path_t = std::vector<std::uint64_t>;

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

	class event;

	class event_iterator {
		event const* _lc = nullptr;
		por::cone::const_reverse_iterator _thread;
		event const* _event = nullptr;
		bool _with_root = true; // include program_init event

	public:
		using value_type = event const*;
		using difference_type = std::ptrdiff_t;
		using pointer = event const* const*;
		using reference = event const* const&;

		using iterator_category = std::forward_iterator_tag;

		event_iterator() = default;

		// this iterator supports different modes:
		// with_root =  true, with_event =  true => [e] (local configuration of e)
		// with_root = false, with_event =  true => [e] \ {program_init} (local configuration without root event)
		// with_root =  true, with_event = false => ⌈e⌉ := [e] \ {e} (causes of e)
		// with_root = false, with_event = false => ⌈e⌉ \ {program_init} (causes of e without root event)
		explicit event_iterator(event const& event, bool with_root=true, bool with_event=true, bool end=false);

		reference operator*() const noexcept { return _event; }
		pointer operator->() const noexcept { return &_event; }

		event_iterator& operator++() noexcept;
		event_iterator operator++(int) noexcept {
			event_iterator tmp = *this;
			++(*this);
			return tmp;
		}

		bool operator==(const event_iterator& rhs) const noexcept {
			return _lc == rhs._lc && _thread == rhs._thread && _event == rhs._event && _with_root == rhs._with_root;
		}
		bool operator!=(const event_iterator& rhs) const noexcept {
			return !(*this == rhs);
		}
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

		// distinct color for immediate_conflicts_sup()
		mutable color_t _imm_cfl_color = 0;
		static color_t _imm_cfl_next_color;

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
		mutable klee::MemoryFingerprintValue _fingerprint;
		mutable klee::MemoryFingerprintDelta _thread_delta;

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

		event_iterator local_configuration_begin(bool include_program_init=true) const noexcept {
			return event_iterator(*this, include_program_init, true);
		}

		event_iterator local_configuration_end(bool include_program_init=true) const noexcept {
			return event_iterator(*this, include_program_init, true, true);
		}

		auto local_configuration(bool include_program_init=true) const noexcept {
			return util::make_iterator_range(local_configuration_begin(include_program_init),
			                                 local_configuration_end(include_program_init));
		}

		event_iterator causes_begin(bool include_program_init=true) const noexcept {
			return event_iterator(*this, include_program_init, false);
		}

		event_iterator causes_end(bool include_program_init=true) const noexcept {
			return event_iterator(*this, include_program_init, false, true);
		}

		auto causes(bool include_program_init=true) const noexcept {
			return util::make_iterator_range(causes_begin(include_program_init), causes_end(include_program_init));
		}

		virtual lock_id_t lid() const noexcept { return 0; }
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

		// computes a superset of immediate conflicts
		std::vector<event const*> immediate_conflicts_sup() const noexcept;

		color_t color() const noexcept { return _color; }
		[[nodiscard]] static color_t new_color() noexcept { return _next_color++; }
		color_t colorize(color_t color) const noexcept { return _color = color; }
		[[nodiscard]] color_t colorize() const noexcept { return colorize(new_color()); }

		template<typename T>
		static color_t colorize(color_t color, T begin, T end) {
			for (; begin != end; ++begin) {
				(*begin)->_color = color;
			}
			return color;
		}

		template<typename T>
		[[nodiscard]] static color_t colorize(T begin, T end) {
			return colorize(new_color(), std::move(begin), std::move(end));
		}
	};
}
