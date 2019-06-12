#pragma once

#include "event/event.h"

#include <algorithm>
#include <map>
#include <utility>
#include <vector>

namespace por {
	class configuration;

	class unfolding {
		std::map<std::tuple<por::event::thread_id_t, std::size_t, por::event::event_kind>, std::vector<std::shared_ptr<por::event::event>>> events;

		// NOTE: do not use for other purposes, only compares pointers of predecessors in cone
		bool compare_events(por::event::event const* a, por::event::event const* b) {
			if(a == b)
				return true;

			if(a->tid() != b->tid())
				return false;

			if(a->depth() != b->depth())
				return false;

			if(a->kind() != b->kind())
				return false;

			if(a->kind() == por::event::event_kind::local) {
				auto alocal = static_cast<por::event::local const*>(a);
				auto blocal = static_cast<por::event::local const*>(b);

				if(alocal->path() != blocal->path())
					return false;
			}

			// compare cone
			for(auto& [tid, c] : a->cone()) {
				if(b->cone().count(tid) == 0)
					return false;
				if(b->cone().at(tid) != c)
					return false;
			}

			return true;
		}

		bool check_path_visited(por::event::event const* e, std::vector<bool> const& path) {
			if(e->visited_paths.empty()) {
				assert(e->visited);
				return true; // no restrictions
			}
			for(auto& visited : e->visited_paths) {
				auto visited_length = visited.size();
				auto path_length = path.size();
				auto mismatch = std::mismatch(visited.begin(), visited.end(), path.begin(), path.end());
				if(mismatch.first == visited.end() && path_length >= visited_length) {
					// all entries present in visited path match new path
					return true;
				}
			}
			return false;
		}

	public:
		unfolding() = delete;
		unfolding(unfolding&) = default;
		unfolding& operator=(unfolding&) = default;
		unfolding(unfolding&&) = default;
		unfolding& operator=(unfolding&&) = default;
		unfolding(por::event::event const* root) {
			mark_as_visited(root);
		}
		~unfolding() = default;

		void mark_as_visited(por::event::event const* e, std::vector<bool>&& path) {
			assert(e != nullptr);
			bool already_visited = e->visited;
			if(!already_visited) {
				events[std::make_tuple(e->tid(), e->depth(), e->kind())].emplace_back(const_cast<por::event::event*>(e)->shared_from_this());
				e->visited = true;
			}
			if(path.empty()) {
				// remove all restrictions
				e->visited_paths.clear();
			} else if(!already_visited || !e->visited_paths.empty()) {
				e->visited_paths.emplace_back(std::move(path));
			}
		}

		void mark_as_visited(por::event::event const* e) {
			mark_as_visited(e, {});
		}

		// return boolean and deduplicated event
		std::pair<bool, por::event::event const*> is_visited(por::event::event const* e, std::vector<bool> const& path) {
			assert(e != nullptr);
			if(e->visited)
				return std::make_pair(check_path_visited(e, path), e);
			if(e->depth() == 0)
				return std::make_pair(true, e); // path is always empty
			auto it = visited.find(std::make_tuple(e->tid(), e->depth(), e->kind()));
			if(it != visited.end()) {
				for(auto v : it->second) {
					assert(v->visited);
					if(compare_events(e, v.get())) {
						assert(e != v.get());
						return std::make_pair(check_path_visited(v.get(), path), v.get());
					}
				}
			}
			return std::make_pair(false, e);
		}

		std::shared_ptr<por::event::event> deduplicate(std::shared_ptr<por::event::event>&& e) {
			if(e->visited || e->depth() == 0)
				return std::move(e); // already in events
			auto it = events.find(std::make_tuple(e->tid(), e->depth(), e->kind()));
			if(it != events.end()) {
				for(auto& v : it->second) {
					if(compare_events(e.get(), v.get()))
						return v;
				}
			}
			// new event
			events[std::make_tuple(e->tid(), e->depth(), e->kind())].emplace_back(e);
			return std::move(e);
		}
	};

	inline std::shared_ptr<por::event::event> por::event::event::deduplicate(std::shared_ptr<por::unfolding>& unfolding, std::shared_ptr<por::event::event>&& event) {
		return unfolding->deduplicate(std::move(event));
	}
}
