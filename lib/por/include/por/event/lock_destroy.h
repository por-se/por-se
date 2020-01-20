#pragma once

#include "base.h"

#include "por/unfolding.h"

#include <cassert>
#include <array>

namespace por::event {
	class lock_destroy final : public event {
		// predecessors:
		// 1. same-thread predecessor
		// 2. previous operation on same lock (may be nullptr if only preceded by lock_create event)
		std::array<event const*, 2> _predecessors;

		lock_id_t _lid;

	protected:
		lock_destroy(thread_id_t tid, lock_id_t lid, event const& thread_predecessor, event const* lock_predecessor)
			: event(event_kind::lock_destroy, tid, thread_predecessor, lock_predecessor)
			, _predecessors{&thread_predecessor, lock_predecessor}
			, _lid(lid)
		{
			assert(this->thread_predecessor());
			assert(this->thread_predecessor()->tid());
			assert(this->thread_predecessor()->tid() == this->tid());
			assert(this->thread_predecessor()->kind() != event_kind::program_init);
			assert(this->thread_predecessor()->kind() != event_kind::thread_exit);

			if(this->lock_predecessor()) {
				assert(this->lock_predecessor()->kind() != event_kind::lock_acquire && "destroying an acquired lock is UB");
				assert(this->lock_predecessor()->kind() == event_kind::lock_create || this->lock_predecessor()->kind() == event_kind::lock_release);
				assert(this->lock_predecessor()->lid() == this->lid());
			}
		}

	public:
		static por::unfolding::deduplication_result alloc(
			unfolding& unfolding,
			thread_id_t tid,
			lock_id_t lid,
			event const& thread_predecessor,
			event const* lock_predecessor
		) {
			return unfolding.deduplicate(lock_destroy{
				tid,
				lid,
				thread_predecessor,
				lock_predecessor
			});
		}

		lock_destroy(lock_destroy&& that)
		: event(std::move(that))
		, _predecessors(that._predecessors)
		, _lid(std::move(that._lid)) {
			that._predecessors = {};
			for(auto& pred : immediate_predecessors_from_cone()) {
				assert(pred != nullptr);
				replace_successor_of(*pred, that);
			}
		}

		~lock_destroy() {
			assert(!has_successors());
			for(auto& pred : immediate_predecessors_from_cone()) {
				assert(pred != nullptr);
				remove_from_successors_of(*pred);
			}
		}

		lock_destroy() = delete;
		lock_destroy(const lock_destroy&) = delete;
		lock_destroy& operator=(const lock_destroy&) = delete;
		lock_destroy& operator=(lock_destroy&&) = delete;

		std::string to_string(bool details) const noexcept override {
			if(details)
				return "[tid: " + tid().to_string() + " depth: " + std::to_string(depth()) + " kind: lock_destroy lid: " + std::to_string(lid()) + (is_cutoff() ? " CUTOFF" : "") + "]";
			return "lock_destroy";
		}

		util::iterator_range<event const* const*> predecessors() const noexcept override {
			if(_predecessors[0] == nullptr) {
				return util::make_iterator_range<event const* const*>(nullptr, nullptr); // only after move-ctor
			} else if(_predecessors[0] != _predecessors[1] && _predecessors[1] != nullptr) {
				return util::make_iterator_range<event const* const*>(_predecessors.data(), _predecessors.data() + 2);
			} else {
				return util::make_iterator_range<event const* const*>(_predecessors.data(), _predecessors.data() + 1);
			}
		}

		event const* thread_predecessor() const noexcept override {
			return _predecessors[0];
		}

		// may return nullptr if only preceded by lock_create event
		event const* lock_predecessor() const noexcept override { return _predecessors[1]; }

		lock_id_t lid() const noexcept override { return _lid; }
	};
}
