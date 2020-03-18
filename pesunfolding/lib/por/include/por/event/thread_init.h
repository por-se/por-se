#pragma once

#include "base.h"

#include "por/unfolding.h"

#include <cassert>
#include <array>

namespace por::event {
	class thread_init final : public event {
		// predecessors:
		// 1. thread creation predecessor (must be a different thread or program init)
		std::array<event const*, 1> _predecessors;

	protected:
		thread_init(thread_id_t tid, event const& creation_predecessor)
			: event(event_kind::thread_init, tid, creation_predecessor)
			, _predecessors{&creation_predecessor}
		{
			assert(this->thread_creation_predecessor());
			assert(this->thread_creation_predecessor()->tid() != this->tid());
			assert(
				(this->thread_creation_predecessor()->kind() == event_kind::program_init
					&& !this->thread_creation_predecessor()->tid())
				|| (this->thread_creation_predecessor()->kind() == event_kind::thread_create
					&& this->thread_creation_predecessor()->tid())
			);

			assert(this->ends_atomic_operation());
			assert(this->atomic_predecessor() == this->thread_creation_predecessor());
		}

	public:
		static por::unfolding::deduplication_result alloc(
			unfolding& unfolding,
			thread_id_t tid,
			event const& creation_predecessor
		) {
			return unfolding.deduplicate(thread_init{
				tid,
				creation_predecessor
			});
		}

		thread_init(thread_init&& that)
		: event(std::move(that))
		, _predecessors(that._predecessors) {
			that._predecessors = {};
			assert(_predecessors.size() == 1);
			assert(thread_creation_predecessor() != nullptr);
		}

		~thread_init() {
			assert(!has_successors());
			if(thread_creation_predecessor() != nullptr) {
				remove_from_successors_of(*thread_creation_predecessor());
			}
		}

		thread_init() = delete;
		thread_init(const thread_init&) = delete;
		thread_init& operator=(const thread_init&) = delete;
		thread_init& operator=(thread_init&&) = delete;

		std::string to_string(bool details) const noexcept override {
			if(details)
				return "[tid: " + tid().to_string() + " depth: " + std::to_string(depth()) + " kind: thread_init" + (is_cutoff() ? " CUTOFF" : "") + "]";
			return "thread_init";
		}

		util::iterator_range<event const* const*> predecessors() const noexcept override {
			return util::make_iterator_range<event const* const*>(_predecessors.data(), _predecessors.data() + _predecessors.size());
		}

		immediate_predecessor_range_t immediate_predecessors() const noexcept override {
			return make_immediate_predecessor_range(predecessors());
		}

		event const* thread_predecessor() const noexcept override {
			return nullptr;
		}

		event const* thread_creation_predecessor() const noexcept { return _predecessors[0]; }

		bool ends_atomic_operation() const noexcept override { return true; }

		event const* atomic_predecessor() const noexcept override {
			return thread_creation_predecessor();
		}
	};
}
