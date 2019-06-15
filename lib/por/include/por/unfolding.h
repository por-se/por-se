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

	public:
		unfolding() = delete;
		unfolding(unfolding&) = default;
		unfolding& operator=(unfolding&) = default;
		unfolding(unfolding&&) = default;
		unfolding& operator=(unfolding&&) = default;
		unfolding(std::shared_ptr<por::event::event> root) {
			events[std::make_tuple(root->tid(), root->depth(), root->kind())].emplace_back(root);
			mark_as_explored(root);
		}
		~unfolding() = default;

		void mark_as_open(std::shared_ptr<por::event::event> const& e, por::event::path_t const& path) {
			assert(e != nullptr);
			e->mark_as_open(path);
		}

		void mark_as_explored(std::shared_ptr<por::event::event> const& e, por::event::path_t const& path) {
			assert(e != nullptr);
			e->mark_as_explored(path);
		}

		void mark_as_explored(std::shared_ptr<por::event::event> const& e) {
			assert(e != nullptr);
			static por::event::path_t empty;
			e->mark_as_explored(empty);
		}

		bool is_present(std::shared_ptr<por::event::event> const& e, por::event::path_t const& path) {
			assert(e != nullptr);
			return e->is_present(path);
		}

		bool is_explored(std::shared_ptr<por::event::event> const& e, por::event::path_t const& path) {
			assert(e != nullptr);
			return e->is_explored(path);
		}

		std::shared_ptr<por::event::event> deduplicate(std::shared_ptr<por::event::event>&& e) {
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
