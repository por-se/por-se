#pragma once

#include "base.h"

#include "por/unfolding.h"

#include <cassert>
#include <array>

namespace por::event {
	template<typename D>
	class local final : public event {
		// predecessors:
		// 1. same-thread predecessor
		std::array<event const*, 1> _predecessors;

		using path_t = std::vector<D>;

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
		static por::unfolding::deduplication_result alloc(
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
		, _predecessors(that._predecessors)
		, _path(std::move(that._path)) {
			that._predecessors = {};
			assert(_predecessors.size() == 1);
			assert(thread_predecessor() != nullptr);
		}

		~local() {
			assert(!has_successors());
			if(thread_predecessor() != nullptr) {
				remove_from_successors_of(*thread_predecessor());
			}
		}

		explicit local() = delete;
		local(const local&) = delete;
		local& operator=(const local&) = delete;
		local& operator=(local&&) = delete;

		std::string path_string() const noexcept override;

		std::string to_string(bool details) const noexcept override {
			if(details) {
				std::string res = "[tid: " + tid().to_string() + " depth: " + std::to_string(depth()) + " kind: local";
				if(!path().empty()) {
					res += " PATH: " + path_string();
				}
				res += "]";
				if (is_cutoff()) {
					res += " CUTOFF";
				}
				return res;
			} else {
				return "local";
			}
		}

		util::iterator_range<event const* const*> predecessors() const noexcept override {
			if(_predecessors[0] == nullptr) {
				return util::make_iterator_range<event const* const*>(nullptr, nullptr); // only after move-ctor
			}
			return util::make_iterator_range<event const* const*>(_predecessors.data(), _predecessors.data() + _predecessors.size());
		}

		immediate_predecessor_range_t immediate_predecessors() const noexcept override {
			return make_immediate_predecessor_range(predecessors());
		}

		event const* thread_predecessor() const noexcept override {
			return _predecessors[0];
		}

		bool has_same_local_path(event const& rhs) const noexcept override {
			// FIXME: add check whether this and rhs use same value for D
			return path() == static_cast<local<D> const&>(rhs).path();
		}

		path_t const& path() const noexcept { return _path; }
	};
}
