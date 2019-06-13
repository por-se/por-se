#pragma once

#include "base.h"

#include <cassert>
#include <array>
#include <memory>

namespace por::event {
	class local final : public event {
		// predecessors:
		// 1. same-thread predecessor
		std::array<std::shared_ptr<event>, 1> _predecessors;

		// decisions taken along path since last local event
		path_t _path;

		exploration_info _info;

	protected:
		local(thread_id_t tid, std::shared_ptr<event>&& thread_predecessor, path_t&& path)
			: event(event_kind::local, tid, thread_predecessor)
			, _predecessors{std::move(thread_predecessor)}
			, _path{std::move(path)}
		{
			assert(this->thread_predecessor());
			assert(this->thread_predecessor()->tid() != 0);
			assert(this->thread_predecessor()->tid() == this->tid());
			assert(this->thread_predecessor()->kind() != event_kind::program_init);
			assert(this->thread_predecessor()->kind() != event_kind::thread_exit);
		}

	public:
		static std::shared_ptr<event> alloc(
			std::shared_ptr<unfolding>& unfolding,
			thread_id_t tid,
			std::shared_ptr<event> thread_predecessor,
			path_t path
		) {
			return deduplicate(unfolding, std::make_shared<local>(
				local{
					tid,
					std::move(thread_predecessor),
					std::move(path)
				}
			));
		}

		virtual void mark_as_open(path_t const& path) const override {
			_info.mark_as_open(path);
		}
		virtual void mark_as_explored(path_t const& path) const override {
			_info.mark_as_explored(path);
		}
		virtual bool is_present(path_t const& path) const override {
			return _info.is_present(path);
		}
		virtual bool is_explored(path_t const& path) const override {
			return _info.is_explored(path);
		}

		virtual std::string to_string(bool details) const override {
			if(details) {
				std::string res = "[tid: " + std::to_string(tid()) + " depth: " + std::to_string(depth()) + " kind: local";
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

		virtual util::iterator_range<std::shared_ptr<event>*> predecessors() override {
			return util::make_iterator_range<std::shared_ptr<event>*>(_predecessors.data(), _predecessors.data() + _predecessors.size());
		}

		virtual util::iterator_range<std::shared_ptr<event> const*> predecessors() const override {
			return util::make_iterator_range<std::shared_ptr<event> const*>(_predecessors.data(), _predecessors.data() + _predecessors.size());
		}

		std::shared_ptr<event>      & thread_predecessor()       noexcept { return _predecessors[0]; }
		std::shared_ptr<event> const& thread_predecessor() const noexcept { return _predecessors[0]; }

		path_t const& path() const noexcept { return _path; }
	};
}
