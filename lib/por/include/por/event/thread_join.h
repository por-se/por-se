#pragma once

#include "base.h"

#include <cassert>
#include <array>
#include <memory>

namespace por::event {
	class thread_join final : public event {
		// predecessors:
		// 1. same-thread predecessor
		// 2. joined thread
		std::array<std::shared_ptr<event>, 2> _predecessors;

	protected:
		thread_join(thread_id_t tid, std::shared_ptr<event>&& thread_predecessor, std::shared_ptr<event>&& joined_thread)
			: event(event_kind::thread_join, tid, thread_predecessor, joined_thread)
			, _predecessors{std::move(thread_predecessor), std::move(joined_thread)}
		{
			assert(this->thread_predecessor());
			assert(this->thread_predecessor()->tid());
			assert(this->thread_predecessor()->tid() == this->tid());
			assert(this->thread_predecessor()->kind() != event_kind::program_init);
			assert(this->thread_predecessor()->kind() != event_kind::thread_exit);
			assert(this->joined_thread());
			assert(this->joined_thread()->tid());
			assert(this->joined_thread()->tid() != this->tid());
			assert(this->joined_thread()->kind() == event_kind::thread_exit);
		}

	public:
		static std::shared_ptr<event> alloc(
			std::shared_ptr<unfolding>& unfolding,
			thread_id_t tid,
			std::shared_ptr<event> thread_predecessor,
			std::shared_ptr<event> joined_thread
		) {
			return deduplicate(unfolding, std::make_shared<thread_join>(
				thread_join{
					tid,
					std::move(thread_predecessor),
					std::move(joined_thread)
				}
			));
		}

		virtual std::string to_string(bool details) const override {
			if(details)
				return "[tid: " + tid().to_string() + " depth: " + std::to_string(depth()) + " kind: thread_join with: " + joined_thread()->tid().to_string() + "]";
			return "thread_join";
		}

		virtual util::iterator_range<std::shared_ptr<event>*> predecessors() override {
			return util::make_iterator_range<std::shared_ptr<event>*>(_predecessors.data(), _predecessors.data() + _predecessors.size());
		}

		virtual util::iterator_range<std::shared_ptr<event> const*> predecessors() const override {
			return util::make_iterator_range<std::shared_ptr<event> const*>(_predecessors.data(), _predecessors.data() + _predecessors.size());
		}

		virtual event const* thread_predecessor() const override {
			return _predecessors[0].get();
		}

		std::shared_ptr<event>      & joined_thread()       noexcept { return _predecessors[1]; }
		std::shared_ptr<event> const& joined_thread() const noexcept { return _predecessors[1]; }
	};
}
