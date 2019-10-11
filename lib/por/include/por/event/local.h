#pragma once

#include "base.h"

#include "por/unfolding.h"

#include <cassert>
#include <array>

namespace por::event {
	class local final : public event {
		// predecessors:
		// 1. same-thread predecessor
		std::array<event const*, 1> _predecessors;

		// decisions taken along path since last local event
		path_t _path;

		exploration_info _info;

	protected:
		local(thread_id_t tid, event const& thread_predecessor, path_t&& path)
			: event(event_kind::local, tid, thread_predecessor)
			, _predecessors{&thread_predecessor}
			, _path{std::move(path)}
		{
			assert(this->thread_predecessor());
			assert(this->thread_predecessor()->tid());
			assert(this->thread_predecessor()->tid() == this->tid());
			assert(this->thread_predecessor()->kind() != event_kind::program_init);
			assert(this->thread_predecessor()->kind() != event_kind::thread_exit);
		}

	public:
		static event const& alloc(
			unfolding& unfolding,
			thread_id_t tid,
			event const& thread_predecessor,
			path_t path
		) {
			return unfolding.deduplicate(local{
				tid,
				thread_predecessor,
				std::move(path)
			});
		}

		void mark_as_open(path_t const& path) const override {
			_info.mark_as_open(path);
		}
		void mark_as_explored(path_t const& path) const override {
			_info.mark_as_explored(path);
		}
		bool is_present(path_t const& path) const override {
			return _info.is_present(path);
		}
		bool is_explored(path_t const& path) const override {
			return _info.is_explored(path);
		}

		std::string to_string(bool details) const override {
			if(details) {
				std::string res = "[tid: " + tid().to_string() + " depth: " + std::to_string(depth()) + " kind: local";
				if(!path().empty())
					res += " PATH:";
				for(auto d: path())
					res += " " + std::to_string(static_cast<unsigned>(d));
				res += "]";
				return res;
			} else {
				return "local";
			}
		}

		util::iterator_range<event const* const*> predecessors() const noexcept override {
			return util::make_iterator_range<event const* const*>(_predecessors.data(), _predecessors.data() + _predecessors.size());
		}

		event const* thread_predecessor() const override {
			return _predecessors[0];
		}

		path_t const& path() const noexcept { return _path; }
	};
}
