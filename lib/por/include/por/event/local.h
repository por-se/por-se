#pragma once

#include "base.h"

#include "por/unfolding.h"

#include <cassert>
#include <array>
#include <sstream>

namespace por::event {
	class local final : public event {
		// predecessors:
		// 1. same-thread predecessor
		std::array<event const*, 1> _predecessors;

		// decisions taken along path since last local event
		path_t _path;

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

		local(local&& that)
		: event(std::move(that))
		, _predecessors(std::move(that._predecessors))
		, _path(std::move(that._path)) {
			assert(_predecessors.size() == 1);
			assert(thread_predecessor() != nullptr);
			replace_successor_of(*thread_predecessor(), that);
		}

		~local() {
			assert(!has_successors());
			assert(_predecessors.size() == 1);
			assert(thread_predecessor() != nullptr);
			remove_from_successors_of(*thread_predecessor());
		}

		explicit local() = delete;
		local(const local&) = delete;
		local& operator=(const local&) = delete;
		local& operator=(local&&) = delete;

		std::string path_string() const noexcept override {
			std::stringstream ss;
			for(auto& p : path()) {
				ss << std::to_string(p);
			}
			return ss.str();
		}

		std::string to_string(bool details) const noexcept override {
			if(details) {
				std::string res = "[tid: " + tid().to_string() + " depth: " + std::to_string(depth()) + " kind: local";
				if(!path().empty()) {
					res += " PATH: " + path_string();
				}
				res += "]";
				return res;
			} else {
				return "local";
			}
		}

		util::iterator_range<event const* const*> predecessors() const noexcept override {
			return util::make_iterator_range<event const* const*>(_predecessors.data(), _predecessors.data() + _predecessors.size());
		}

		event const* thread_predecessor() const noexcept override {
			return _predecessors[0];
		}

		bool has_same_local_path(event const& rhs) const noexcept override {
			return path() == static_cast<local const&>(rhs).path();
		}

		path_t const& path() const noexcept { return _path; }
	};
}
