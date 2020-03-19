#pragma once

#include "iterator.h"
#include "kind.h"

#include "klee/Fingerprint/MemoryFingerprintDelta.h"
#include "klee/Fingerprint/MemoryFingerprintValue.h"

#include "por/cone.h"
#include "por/thread_id.h"

#include "util/check.h"
#include "util/iterator_range.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace por {
	class unfolding;
}

namespace por::event {
	using thread_id_t = por::thread_id;
	using lock_id_t = std::uint64_t;
	using cond_id_t = std::uint64_t;
	using fingerprint_delta_t = klee::MemoryFingerprintDelta;
	using fingerprint_value_t = klee::MemoryFingerprintValue;

	class event {
		friend class por::unfolding; // for caching of immediate_conflicts

	public:
		using depth_t = std::size_t;
		using color_t = std::size_t;

	private:
		depth_t _depth;
		por::cone _cone; // maximal predecessor per thread (excl. program_init)
		thread_id_t _tid;
		event_kind _kind;

		mutable color_t _color = 0;

		// distinct color for compute_immediate_conflicts()
		mutable color_t _imm_cfl_color = 0;

		// events that have this as immediate predecessor
		mutable std::vector<event const*> _successors;

		mutable std::vector<event const*> _immediate_conflicts;
		std::vector<event const*> compute_immediate_conflicts() const noexcept;

		void clear_cache_immediate_conflicts() const noexcept {
			_immediate_conflicts.clear();
		}

		void remove_from_immediate_conflicts(event const& event) const noexcept {
			auto it = std::find(_immediate_conflicts.begin(), _immediate_conflicts.end(), &event);
			if(it == _immediate_conflicts.end()) {
				return;
			}
			_immediate_conflicts.erase(it);
		}

		mutable bool _fingerprint_set = false;
		mutable fingerprint_value_t _fingerprint;
		mutable fingerprint_delta_t _thread_delta;

	public:
		bool has_fingerprint() const noexcept { return _fingerprint_set; }

		fingerprint_value_t const& fingerprint() const noexcept {
			assert(has_fingerprint());
			return _fingerprint;
		}

		fingerprint_delta_t const& thread_delta() const noexcept {
			assert(has_fingerprint());
			return _thread_delta;
		}

		bool set_fingerprint(fingerprint_value_t fingerprint, fingerprint_delta_t thread_delta) const noexcept {
			if(has_fingerprint()) {
				return thread_delta == _thread_delta && fingerprint == _fingerprint;
			}
			_fingerprint_set = true;
			_thread_delta = thread_delta;
			_fingerprint = fingerprint;
			return true;
		}

		mutable bool _is_cutoff = false;

		bool is_cutoff() const noexcept { return _is_cutoff; }

		mutable std::size_t _lc_size = 0;

		event_kind kind() const noexcept { return _kind; }
		thread_id_t const& tid() const noexcept { return _tid; }
		depth_t depth() const noexcept { return _depth; }
		auto const& cone() const noexcept { return _cone; }

		event(event&& that)
		: _depth(that._depth)
		, _cone(std::move(that._cone))
		, _tid(std::move(that._tid))
		, _kind(that._kind)
		, _color(that._color)
		, _imm_cfl_color(that._imm_cfl_color)
		, _successors(std::move(that._successors))
		, _immediate_conflicts(std::move(that._immediate_conflicts))
		, _fingerprint_set(that._fingerprint_set)
		, _fingerprint(std::move(that._fingerprint))
		, _thread_delta(std::move(that._thread_delta))
		, _is_cutoff(that._is_cutoff)
		, _lc_size(that._lc_size) {
			assert(!has_successors());
		}

		virtual ~event() {
			assert(!has_successors());
		}

		event() = delete;
		event(const event&) = delete;
		event& operator=(const event&) = delete;
		event& operator=(event&&) = delete;

	protected:
		std::vector<event const*> immediate_predecessors_from_cone() const noexcept;

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
		, _is_cutoff(immediate_predecessor._is_cutoff)
		{
			assert(immediate_predecessor._depth < _depth);
			libpor_check(_cone.size() >= immediate_predecessor._cone.size());
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
				if(event->_is_cutoff) {
					_is_cutoff = true;
				}
			}
			_depth = max_depth + 1;

			assert(immediate_predecessor._depth < _depth);
			libpor_check(_cone.size() >= immediate_predecessor._cone.size());
			if(single_other_predecessor != nullptr) {
				assert(single_other_predecessor->_depth < _depth);
				libpor_check(_cone.size() >= single_other_predecessor->_cone.size());
			}
			for(auto& op : other_predecessors) {
				if(!op) {
					continue;
				}
				assert(op->_depth < _depth);
				libpor_check(_cone.size() >= op->_cone.size());
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

		void add_to_successors() const noexcept {
			for(auto& p : immediate_predecessors()) {
				p->_successors.push_back(this);
			}
		}

		void remove_from_successors_of(event const& event) const noexcept {
			auto it = std::find(event._successors.begin(), event._successors.end(), this);
			if(it != event._successors.end()) {
				if(event._successors.size() > 1) {
					std::iter_swap(it, event._successors.end() - 1);
					event._successors.pop_back();
				} else {
					assert(event._successors.empty() || event._successors.front() == this);
					event._successors.clear();
				}
			}
		}

	public:
		bool has_successors() const noexcept {
			return !_successors.empty();
		}

		virtual std::string path_string() const noexcept {
			return "";
		}

		virtual std::string to_string(bool details = false) const noexcept = 0;

		virtual util::iterator_range<event const* const*> predecessors() const noexcept {
			assert(_kind == event_kind::program_init);
			return util::make_iterator_range<event const* const*>(nullptr, nullptr);
		}

	private:
		template<typename T>
		class storing_iterator {
			std::shared_ptr<T> _ptr;
			typename T::iterator _it;

		public:
			using value_type = por::event::event const*;
			using difference_type = std::ptrdiff_t;
			using pointer = por::event::event const* const*;
			using reference = por::event::event const* const&;

			using iterator_category = std::forward_iterator_tag;

			storing_iterator() = default;
			explicit storing_iterator(std::shared_ptr<T> ptr, bool end = false) noexcept : _ptr(ptr) {
				if(!end) {
					_it = ptr->begin();
				} else {
					_it = ptr->end();
				}
			}

			reference operator*() const noexcept { return *_it; }
			pointer operator->() const noexcept { return &*_it; }

			storing_iterator& operator++() noexcept {
				if(!_ptr) {
					return *this;
				}

				if(_it != _ptr->end()) {
					++_it;
				}

				return *this;
			}
			storing_iterator operator++(int) noexcept {
				storing_iterator tmp = *this;
				++(*this);
				return tmp;
			}

			bool operator==(const storing_iterator<T>& rhs) const noexcept {
				libpor_check(decltype(_it)() == decltype(_it)());
				return _ptr == rhs._ptr && _it == rhs._it;
			}
			bool operator!=(const storing_iterator<T>& rhs) const noexcept {
				return !(*this == rhs);
			}
		};

		class imm_pred_iterator {
			using storing_vector_t = storing_iterator<std::vector<event const*>>;
			using iterator_range_t = event const* const*;

			std::variant<storing_vector_t, iterator_range_t> _it;

		public:
			using value_type = por::event::event const*;
			using difference_type = std::ptrdiff_t;
			using pointer = por::event::event const* const*;
			using reference = por::event::event const* const&;

			using iterator_category = std::forward_iterator_tag;

			imm_pred_iterator() = default;

		private:
			imm_pred_iterator(util::iterator_range<event const* const*> range, bool end = false)
			: _it(end ? range.end() : range.begin())
			{ }
			imm_pred_iterator(event const* const* it)
			: _it(it)
			{ }
			imm_pred_iterator(std::shared_ptr<std::vector<event const*>> ptr, bool end = false)
			: _it(storing_vector_t(ptr, end))
			{ }

		public:
			static auto make_range_from_range(util::iterator_range<event const* const*> &&range) {
				return util::make_iterator_range(imm_pred_iterator(range), imm_pred_iterator(range, true));
			}
			static auto make_range_from_range(event const* const* begin, event const* const* end) {
				return util::make_iterator_range(imm_pred_iterator(begin), imm_pred_iterator(end));
			}
			static auto make_range_from_vector(std::shared_ptr<std::vector<event const*>> ptr) {
				return util::make_iterator_range(imm_pred_iterator(ptr), imm_pred_iterator(ptr, true));
			}

			reference operator*() const noexcept {
				if(std::holds_alternative<storing_vector_t>(_it)) {
					return *std::get<storing_vector_t>(_it);
				} else {
					return *std::get<iterator_range_t>(_it);
				}
			}

			pointer operator->() const noexcept {
				if(std::holds_alternative<storing_vector_t>(_it)) {
					return &*std::get<storing_vector_t>(_it);
				} else {
					return &*std::get<iterator_range_t>(_it);
				}
			}

			imm_pred_iterator& operator++() noexcept {
				if(std::holds_alternative<storing_vector_t>(_it)) {
					std::advance(std::get<storing_vector_t>(_it), 1);
				} else {
					std::advance(std::get<iterator_range_t>(_it), 1);
				}
				return *this;
			}

			imm_pred_iterator operator++(int) noexcept {
				imm_pred_iterator tmp = *this;
				++(*this);
				return tmp;
			}

			bool operator==(const imm_pred_iterator& rhs) const noexcept {
				libpor_check(decltype(_it)() == decltype(_it)());
				return _it == rhs._it;
			}
			bool operator!=(const imm_pred_iterator& rhs) const noexcept {
				return !(*this == rhs);
			}
		};

	protected:
		using immediate_predecessor_range_t = util::iterator_range<imm_pred_iterator>;
		static auto make_immediate_predecessor_range(util::iterator_range<event const* const*> range) noexcept {
			return imm_pred_iterator::make_range_from_range(std::move(range));
		}
		static auto make_immediate_predecessor_range(event const* const* begin, event const* const* end) noexcept {
			return imm_pred_iterator::make_range_from_range(begin, end);
		}
		static auto make_immediate_predecessor_range(std::shared_ptr<std::vector<event const*>> ptr) noexcept {
			return imm_pred_iterator::make_range_from_vector(std::move(ptr));
		}

	public:
		virtual immediate_predecessor_range_t immediate_predecessors() const noexcept {
			auto ptr = std::make_shared<std::vector<event const*>>(immediate_predecessors_from_cone());
			return make_immediate_predecessor_range(std::move(ptr));
		}

		auto const& successors() const noexcept {
			return _successors;
		}

		virtual event const* thread_predecessor() const noexcept = 0;

		virtual event const* lock_predecessor() const noexcept {
			return nullptr;
		}

		virtual util::iterator_range<event const* const*> condition_variable_predecessors() const noexcept {
			return util::make_iterator_range<event const* const*>(nullptr, nullptr);
		}

		virtual bool has_same_local_path(event const&) const noexcept {
			return true;
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

		std::size_t local_configuration_size() const noexcept {
			if(!_lc_size) {
				_lc_size = local_configuration().size();
			}
			return _lc_size;
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

		virtual bool ends_atomic_operation() const noexcept { return false; }
		virtual event const* atomic_predecessor() const noexcept { return nullptr; }

		bool is_independent_of(event const* other) const noexcept;

		// IMPORTANT: assumes no conflict between this and rhs
		bool is_less_than(por::cone const& rhs) const noexcept {
			auto it = rhs.find(_tid);
			if(it != rhs.end()) {
				event const& e = *it->second;
				return _depth < e._depth || _depth == e._depth;
			} else if (kind() == event_kind::program_init) {
				return true;
			}
			return false;
		}

		// IMPORTANT: assumes no conflict between this and rhs
		bool is_less_than(event const& rhs) const noexcept {
			if(rhs._tid == _tid) {
				return _depth < rhs._depth;
			} else {
				return is_less_than(rhs.cone());
			}
			return false;
		}

		// IMPORTANT: assumes no conflict between this and rhs
		bool is_less_than_eq(event const& rhs) const noexcept {
			return (&rhs == this) || is_less_than(rhs);
		}

		bool is_enabled(configuration const&) const noexcept;

		std::size_t mark_as_cutoff() const noexcept;

		std::vector<event const*> const& immediate_conflicts() const noexcept {
			return _immediate_conflicts;
		}

		color_t color() const noexcept { return _color; }
		[[nodiscard]] static color_t new_color() noexcept {
			static color_t next_color;
			return ++next_color;
		}
		color_t colorize(color_t color) const noexcept { return _color = color; }
		[[nodiscard]] color_t colorize() const noexcept { return colorize(new_color()); }

		template<typename T>
		static color_t colorize(color_t color, T begin, T end) {
			for(; begin != end; ++begin) {
				(*begin)->_color = color;
			}
			return color;
		}

		template<typename T>
		[[nodiscard]] static color_t colorize(T begin, T end) {
			return colorize(new_color(), std::move(begin), std::move(end));
		}

	private:
		[[nodiscard]] static color_t new_cfl_color() noexcept {
			static color_t next_cfl_color;
			return ++next_cfl_color;
		}
	};
}
