#pragma once

#include "event/event.h"

#include <algorithm>
#include <map>
#include <utility>
#include <vector>

namespace por {
	class configuration;

	class unfolding {
		std::map<std::tuple<por::event::thread_id_t, std::size_t, por::event::event_kind>, std::vector<std::shared_ptr<por::event::event const>>> visited;

		// NOTE: do not use for other purposes, only compares pointers of predecessors
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

			auto a_preds = a->predecessors();
			auto b_preds = b->predecessors();
			std::size_t a_num_preds = std::distance(a_preds.begin(), a_preds.end());
			std::size_t b_num_preds = std::distance(b_preds.begin(), b_preds.end());

			if(a_num_preds != b_num_preds)
				return false;

			auto a_it = a_preds.begin();
			auto a_ie = a_preds.end();
			auto b_it = b_preds.begin();
			auto b_ie = b_preds.end();
			for(std::size_t i = 0; i < a_num_preds; ++i) {
				assert(a_it != a_ie);
				assert(b_it != b_ie);
				if(*a_it != *b_it)
					return false;
				++a_it;
				++b_it;
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
				visited[std::make_tuple(e->tid(), e->depth(), e->kind())].emplace_back(e->shared_from_this());
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
	};
}
