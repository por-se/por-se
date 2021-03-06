#pragma once

#include "event/base.h"
#include "thread_id.h"

#include <map>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include <iostream>

namespace por {
	class configuration;

	class unfolding {
	public:
		struct deduplication_result {
			bool unknown;
			por::event::event const& event;

			operator por::event::event const&() const noexcept {
				return event;
			}

			operator por::event::event const*() const noexcept {
				return &event;
			}
		};

	private:
		using key_t = std::tuple<por::event::thread_id_t, std::size_t, por::event::event_kind>;
		using value_t = std::vector<std::unique_ptr<por::event::event>>;

		std::map<key_t, value_t> _events;
		por::event::event const* _root;

		std::size_t _size;

		por::event::event const* store_event(std::unique_ptr<por::event::event>&& event) {
			key_t key = std::make_tuple(event->tid(), event->depth(), event->kind());
			auto ptr = _events[std::move(key)].emplace_back(std::move(event)).get();
			stats_inc_unique_event(ptr->kind());
			++_size;
			ptr->_metadata.id = _size;
			return ptr;
		}

	public:
		unfolding();
		unfolding(unfolding const&) = default;
		unfolding& operator=(unfolding const&) = default;
		unfolding(unfolding&&) = default;
		unfolding& operator=(unfolding&&) = default;

		~unfolding() {
			assert(_root->has_successors());
			while(!_events.empty()) {
				[[maybe_unused]] std::size_t removed = 0;
				for(auto it = _events.begin(); it != _events.end();) {
					auto& v = it->second;
					for(auto it2 = v.begin(); it2 != v.end();) {
						auto& e = *it2;
						if(e->kind() == por::event::event_kind::program_init) {
							assert(e.get() == _root);
						}
						if(!e->has_successors()) {
							it2 = v.erase(it2);
							++removed;
						} else {
							++it2;
						}
					}
					if(v.empty()) {
						it = _events.erase(it);
					} else {
						++it;
					}
				}
				assert(removed > 0 && "infinite loop");
			}
		}

		// NOTE: shallow compare, only compares pointers of predecessors
		static bool compare_events(por::event::event const& a, por::event::event const& b);

		deduplication_result deduplicate(std::unique_ptr<por::event::event>&& e) {
			auto it = _events.find(std::make_tuple(e->tid(), e->depth(), e->kind()));
			if(it != _events.end()) {
				for(auto& v : it->second) {
					if(compare_events(*e.get(), *v.get())) {
						stats_inc_event_deduplicated();
						if(e->is_cutoff()) {
							v->mark_as_cutoff();
						}
						if(v->metadata() == por::event::metadata{}) {
							v->set_metadata(std::move(e->_metadata)); // FIXME: improve this
						}
						return {false, *v.get()};
					}
				}
			}
			// new event
			auto ptr = store_event(std::move(e));
			ptr->add_to_successors();

			ptr->_immediate_conflicts = ptr->compute_immediate_conflicts();
			for(auto const* other : ptr->_immediate_conflicts) {
#ifdef LIBPOR_CHECKED
				auto other_cfls = other->compute_immediate_conflicts();
				libpor_check(std::find(other_cfls.begin(), other_cfls.end(), ptr) != other_cfls.end());
#endif
				other->_immediate_conflicts.push_back(ptr);
			}
			return {true, *ptr};
		}

		void remove_event(por::event::event const& e) {
			auto it = _events.find(std::make_tuple(e.tid(), e.depth(), e.kind()));
			if(it != _events.end()) {
				auto& events = it->second;
				events.erase(std::remove_if(events.begin(), events.end(), [this, &e](auto& v) {
					if(&e != v.get()) {
						return false;
					}

					for(auto& ic : e.immediate_conflicts()) {
						auto it = std::find(ic->_immediate_conflicts.begin(), ic->_immediate_conflicts.end(), &e);
						if(it == ic->_immediate_conflicts.end()) {
							continue;
						}
						ic->_immediate_conflicts.erase(it);
					}
					--_size;
					return true;
				}), events.end());
			}
		}

		por::event::event const& root() {
			return *_root;
		}

		std::size_t size() const noexcept {
			return _size;
		}

		por::event::event const* compute_alternative(por::configuration const& C,
		                                             std::vector<por::event::event const*> D) const noexcept;

		// statistics
	private:
		std::array<std::size_t, 16> _events_created{};
		std::array<std::size_t, 16> _unique_events{};
		std::array<std::size_t, 16> _cutoff_events{};
		std::size_t _events_deduplicated = 0; // total number of deduplicated events
		std::size_t _cex_created = 0; // number of conflicting extensions generated
		std::size_t _cex_inserted = 0; // number of actual conflicting extensions inserted
		std::size_t _configurations = 0; // number of times cex generation was called (NOT necessarily maximal)

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
					assert(0 && "unknown event_kind");
					return 255;
			}
		}

	public:
		void stats_inc_event_created(por::event::event_kind kind) noexcept {
			assert(kind_index(kind) < _events_created.size());
			++_events_created[kind_index(kind)];
		}
		void stats_inc_unique_event(por::event::event_kind kind) noexcept {
			assert(kind_index(kind) < _unique_events.size());
			++_unique_events[kind_index(kind)];
		}
		void stats_inc_cutoff_event(por::event::event_kind kind) noexcept {
			assert(kind_index(kind) < _cutoff_events.size());
			++_cutoff_events[kind_index(kind)];
		}
		void stats_inc_event_deduplicated() noexcept {
			++_events_deduplicated;
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

		void print_statistics() {
			std::cout.flush();
			std::cout << "\n\n";
			std::cout << "== UNFOLDING STATISTICS ==\n";
			std::cout << "Events created: ";
			std::size_t created_events = 0;
			for (std::size_t count : _events_created) {
				created_events += count;
			}
			std::cout << created_events << "\n";
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
			std::cout << "Unique Events: ";
			std::size_t unique_events = 0;
			for (std::size_t count : _unique_events) {
				unique_events += count;
			}
			std::cout << unique_events << "\n";
			std::cout << ". local: " << _unique_events[kind_index(por::event::event_kind::local)] << "\n";
			std::cout << ". program_init: " << _unique_events[kind_index(por::event::event_kind::program_init)] << "\n";
			std::cout << ". thread_create: " << _unique_events[kind_index(por::event::event_kind::thread_create)] << "\n";
			std::cout << ". thread_join: " << _unique_events[kind_index(por::event::event_kind::thread_join)] << "\n";
			std::cout << ". thread_init: " << _unique_events[kind_index(por::event::event_kind::thread_init)] << "\n";
			std::cout << ". thread_exit: " << _unique_events[kind_index(por::event::event_kind::thread_exit)] << "\n";
			std::cout << ". lock_create: " << _unique_events[kind_index(por::event::event_kind::lock_create)] << "\n";
			std::cout << ". lock_destroy: " << _unique_events[kind_index(por::event::event_kind::lock_destroy)] << "\n";
			std::cout << ". lock_acquire: " << _unique_events[kind_index(por::event::event_kind::lock_acquire)] << "\n";
			std::cout << ". lock_release: " << _unique_events[kind_index(por::event::event_kind::lock_release)] << "\n";
			std::cout << ". condition_variable_create: " << _unique_events[kind_index(por::event::event_kind::condition_variable_create)] << "\n";
			std::cout << ". condition_variable_destroy: " << _unique_events[kind_index(por::event::event_kind::condition_variable_destroy)] << "\n";
			std::cout << ". wait1: " << _unique_events[kind_index(por::event::event_kind::wait1)] << "\n";
			std::cout << ". wait2: " << _unique_events[kind_index(por::event::event_kind::wait2)] << "\n";
			std::cout << ". signal: " << _unique_events[kind_index(por::event::event_kind::signal)] << "\n";
			std::cout << ". broadcast: " << _unique_events[kind_index(por::event::event_kind::broadcast)] << "\n";
			std::cout << "Cutoff Events: ";
			std::size_t cutoff_events = 0;
			for (std::size_t count : _cutoff_events) {
				cutoff_events += count;
			}
			std::cout << cutoff_events << "\n";
			std::cout << "x local: " << _cutoff_events[kind_index(por::event::event_kind::local)] << "\n";
			std::cout << "x program_init: " << _cutoff_events[kind_index(por::event::event_kind::program_init)] << "\n";
			std::cout << "x thread_create: " << _cutoff_events[kind_index(por::event::event_kind::thread_create)] << "\n";
			std::cout << "x thread_join: " << _cutoff_events[kind_index(por::event::event_kind::thread_join)] << "\n";
			std::cout << "x thread_init: " << _cutoff_events[kind_index(por::event::event_kind::thread_init)] << "\n";
			std::cout << "x thread_exit: " << _cutoff_events[kind_index(por::event::event_kind::thread_exit)] << "\n";
			std::cout << "x lock_create: " << _cutoff_events[kind_index(por::event::event_kind::lock_create)] << "\n";
			std::cout << "x lock_destroy: " << _cutoff_events[kind_index(por::event::event_kind::lock_destroy)] << "\n";
			std::cout << "x lock_acquire: " << _cutoff_events[kind_index(por::event::event_kind::lock_acquire)] << "\n";
			std::cout << "x lock_release: " << _cutoff_events[kind_index(por::event::event_kind::lock_release)] << "\n";
			std::cout << "x condition_variable_create: " << _cutoff_events[kind_index(por::event::event_kind::condition_variable_create)] << "\n";
			std::cout << "x condition_variable_destroy: " << _cutoff_events[kind_index(por::event::event_kind::condition_variable_destroy)] << "\n";
			std::cout << "x wait1: " << _cutoff_events[kind_index(por::event::event_kind::wait1)] << "\n";
			std::cout << "x wait2: " << _cutoff_events[kind_index(por::event::event_kind::wait2)] << "\n";
			std::cout << "x signal: " << _cutoff_events[kind_index(por::event::event_kind::signal)] << "\n";
			std::cout << "x broadcast: " << _cutoff_events[kind_index(por::event::event_kind::broadcast)] << "\n";
			std::cout << "Events deduplicated: " << std::to_string(_events_deduplicated) << "\n";
			std::cout << "CEX created: " << std::to_string(_cex_created) << "\n";
			std::cout << "CEX inserted: " << std::to_string(_cex_inserted) << "\n";
			std::cout << "Configurations: " << std::to_string(_configurations) << "\n";
			std::cout << "==========================\n";
			std::cout.flush();
		}
	};
}
