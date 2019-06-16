#pragma once

#include "event/event.h"

#include <algorithm>
#include <map>
#include <utility>
#include <vector>

#include <iostream>

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
					if(compare_events(e.get(), v.get())) {
						stats_inc_event_deduplicated();
						return v;
					}
				}
			}
			// new event
			events[std::make_tuple(e->tid(), e->depth(), e->kind())].emplace_back(e);
			stats_inc_unique_event(e->kind());
			return std::move(e);
		}


		// statistics
	private:
		std::array<std::size_t, 16> _events_created{};
		std::array<std::size_t, 16> _unique_events{};
		std::size_t _events_deduplicated = 0; // total number of deduplicated events
		std::size_t _cex_candidates = 0; // number of candidates (before visited)
		std::size_t _cex_created = 0; // number of conflicting extensions generated
		std::size_t _cex_inserted = 0; // number of actual conflicting extensions inserted
		std::size_t _configurations = 0; // number of times cex generation was called (NOT necessarily maximal)

		// together with _cex_inserted: average
		std::size_t _sum_of_conflict_gaps = 0; // conflict gap: number of events whose standby states are no longer valid after cex generation
		std::size_t _sum_of_catchup_gaps = 0; // catch-up gap: number of events from last valid standby state until actual standby state

		constexpr std::uint8_t kind_index(por::event::event_kind kind) const noexcept {
			switch(kind) {
				case por::event::event_kind::local:
					return 0;
				case por::event::event_kind::program_init:
					return 1;
				case por::event::event_kind::thread_create:
					return 2;
				case por::event::event_kind::thread_join:
					return 3;
				case por::event::event_kind::thread_init:
					return 4;
				case por::event::event_kind::thread_exit:
					return 5;
				case por::event::event_kind::lock_create:
					return 6;
				case por::event::event_kind::lock_destroy:
					return 7;
				case por::event::event_kind::lock_acquire:
					return 8;
				case por::event::event_kind::lock_release:
					return 9;
				case por::event::event_kind::condition_variable_create:
					return 10;
				case por::event::event_kind::condition_variable_destroy:
					return 11;
				case por::event::event_kind::wait1:
					return 12;
				case por::event::event_kind::wait2:
					return 13;
				case por::event::event_kind::signal:
					return 14;
				case por::event::event_kind::broadcast:
					return 15;
				default:
					assert(0);
			}
		}

	public:
		void stats_inc_event_created(por::event::event_kind kind) noexcept {
			assert(kind_index(kind) < _events_created.size());
			++_events_created[kind_index(kind)];
		}
		void stats_inc_unique_event(por::event::event_kind kind) noexcept {
			assert(kind_index(kind) < _events_created.size());
			++_unique_events[kind_index(kind)];
		}
		void stats_inc_event_deduplicated() noexcept {
			++_events_deduplicated;
		}
		void stats_inc_cex_candidates(std::size_t inc) noexcept {
			_cex_candidates += inc;
		}
		void stats_inc_cex_created(std::size_t inc) noexcept {
			_cex_created += inc;
		}
		void stats_inc_cex_inserted() noexcept {
			++_cex_inserted;
		}
		void stats_inc_configuration() noexcept {
			++_configurations;
		}
		void stats_inc_sum_of_conflict_gaps(std::size_t inc) noexcept {
			_sum_of_conflict_gaps += inc;
		}
		void stats_inc_sum_of_catchup_gaps(std::size_t inc) noexcept {
			_sum_of_catchup_gaps += inc;
		}

		void print_statistics() {
			std::cout << "\n\n";
			std::cout << "== UNFOLDING STATISTICS ==\n";
			std::cout << "Events created:\n";
			std::cout << "  local: " << _events_created[kind_index(por::event::event_kind::local)] << "\n";
			std::cout << "  program_init: " << _events_created[kind_index(por::event::event_kind::program_init)] << "\n";
			std::cout << "  thread_create: " << _events_created[kind_index(por::event::event_kind::thread_create)] << "\n";
			std::cout << "  thread_join: " << _events_created[kind_index(por::event::event_kind::thread_join)] << "\n";
			std::cout << "  thread_init: " << _events_created[kind_index(por::event::event_kind::thread_init)] << "\n";
			std::cout << "  thread_exit: " << _events_created[kind_index(por::event::event_kind::thread_exit)] << "\n";
			std::cout << "  lock_create: " << _events_created[kind_index(por::event::event_kind::lock_create)] << "\n";
			std::cout << "  lock_destroy: " << _events_created[kind_index(por::event::event_kind::lock_destroy)] << "\n";
			std::cout << "  lock_acquire: " << _events_created[kind_index(por::event::event_kind::lock_acquire)] << "\n";
			std::cout << "  lock_release: " << _events_created[kind_index(por::event::event_kind::lock_release)] << "\n";
			std::cout << "  condition_variable_create: " << _events_created[kind_index(por::event::event_kind::condition_variable_create)] << "\n";
			std::cout << "  condition_variable_destroy: " << _events_created[kind_index(por::event::event_kind::condition_variable_destroy)] << "\n";
			std::cout << "  wait1: " << _events_created[kind_index(por::event::event_kind::wait1)] << "\n";
			std::cout << "  wait2: " << _events_created[kind_index(por::event::event_kind::wait2)] << "\n";
			std::cout << "  signal: " << _events_created[kind_index(por::event::event_kind::signal)] << "\n";
			std::cout << "  broadcast: " << _events_created[kind_index(por::event::event_kind::broadcast)] << "\n";
			std::cout << "Unique Events:\n";
			std::cout << "  local: " << _unique_events[kind_index(por::event::event_kind::local)] << "\n";
			std::cout << "  program_init: " << _unique_events[kind_index(por::event::event_kind::program_init)] << "\n";
			std::cout << "  thread_create: " << _unique_events[kind_index(por::event::event_kind::thread_create)] << "\n";
			std::cout << "  thread_join: " << _unique_events[kind_index(por::event::event_kind::thread_join)] << "\n";
			std::cout << "  thread_init: " << _unique_events[kind_index(por::event::event_kind::thread_init)] << "\n";
			std::cout << "  thread_exit: " << _unique_events[kind_index(por::event::event_kind::thread_exit)] << "\n";
			std::cout << "  lock_create: " << _unique_events[kind_index(por::event::event_kind::lock_create)] << "\n";
			std::cout << "  lock_destroy: " << _unique_events[kind_index(por::event::event_kind::lock_destroy)] << "\n";
			std::cout << "  lock_acquire: " << _unique_events[kind_index(por::event::event_kind::lock_acquire)] << "\n";
			std::cout << "  lock_release: " << _unique_events[kind_index(por::event::event_kind::lock_release)] << "\n";
			std::cout << "  condition_variable_create: " << _unique_events[kind_index(por::event::event_kind::condition_variable_create)] << "\n";
			std::cout << "  condition_variable_destroy: " << _unique_events[kind_index(por::event::event_kind::condition_variable_destroy)] << "\n";
			std::cout << "  wait1: " << _unique_events[kind_index(por::event::event_kind::wait1)] << "\n";
			std::cout << "  wait2: " << _unique_events[kind_index(por::event::event_kind::wait2)] << "\n";
			std::cout << "  signal: " << _unique_events[kind_index(por::event::event_kind::signal)] << "\n";
			std::cout << "  broadcast: " << _unique_events[kind_index(por::event::event_kind::broadcast)] << "\n";
			std::cout << "Events deduplicated: " << std::to_string(_events_deduplicated) << "\n";
			std::cout << "CEX candidates: " << std::to_string(_cex_candidates) << "\n";
			std::cout << "CEX created: " << std::to_string(_cex_created) << "\n";
			std::cout << "CEX inserted: " << std::to_string(_cex_inserted) << "\n";
			std::cout << "Configurations: " << std::to_string(_configurations) << "\n";
			std::cout << "Sum of conflict gaps: " << std::to_string(_sum_of_conflict_gaps) << "\n";
			std::cout << "Sum of catch-up gaps: " << std::to_string(_sum_of_catchup_gaps) << "\n";
			std::cout << "==========================\n";
		}
	};

	inline std::shared_ptr<por::event::event> por::event::event::deduplicate(std::shared_ptr<por::unfolding>& unfolding, std::shared_ptr<por::event::event>&& event) {
		return unfolding->deduplicate(std::move(event));
	}
}
