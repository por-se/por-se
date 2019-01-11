#include <por/configuration.h>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <random>
#include <iostream>
#include <iomanip>
#include <set>

#include <util/sso_array.h>

namespace {
	por::event::thread_id_t choose_thread(por::configuration const& configuration, std::mt19937_64& gen) {
		std::uniform_int_distribution<std::size_t> dis(1, configuration.active_threads());
		std::size_t const chosen = dis(gen);
		std::size_t count = 0;
		for(auto it = configuration.thread_heads().begin(); ; ++it){
			assert(it != configuration.thread_heads().end());
			if(it->second->kind() != por::event::event_kind::thread_exit) {
				++count;
				if(count == chosen) {
					assert(it->first == it->second->tid());
					return it->first;
				}
			}
		}
		assert(false && "Active thread number was too large");
		std::abort();
	}

	por::event::lock_id_t choose_lock(por::configuration const& configuration, std::mt19937_64& gen) {
		assert(!configuration.lock_heads().empty());

		std::uniform_int_distribution<std::size_t> dis(0, configuration.lock_heads().size() - 1);
		std::size_t chosen = dis(gen);
		return std::next(configuration.lock_heads().begin(), chosen)->first;
	}
}

int main(int argc, char** argv){
	assert(argc > 0);

	por::configuration configuration; // construct a default configuration with 1 main thread

#ifdef SEED
	std::mt19937_64 gen(SEED);
#else
	std::mt19937_64 gen(35);
#endif
	// "warm up" mersenne twister to deal with weak initialization function
	for(unsigned i = 0; i < 10'000; ++i) {
		static_cast<void>(gen());
	}
	std::uniform_int_distribution<unsigned> event_dis(0, 999);
	std::bernoulli_distribution rare_choice(0.1);

	while(configuration.active_threads() > 0) {
		auto const roll = event_dis(gen);
		std::cout << "   r " << std::setw(3) << roll << "\n";
		if(roll < 40) {
			// spawn new thread
			auto source = choose_thread(configuration, gen);
			auto tid = configuration.spawn_thread(source);
			std::cout << "+T " << tid << " (" << source << ")\n";
		} else if(roll < 100) {
			// kill old thread
			auto tid = choose_thread(configuration, gen);
			configuration.exit_thread(tid);
			std::cout << "-T " << tid << "\n";
		} else if(roll < 200) {
			// spawn new lock
			auto tid = choose_thread(configuration, gen);
			auto lid = configuration.create_lock(tid);
			std::cout << "+L " << lid << " (" << tid << ")\n";
		} else if(roll < 300) {
			// destroy lock, if one exists
			if(!configuration.lock_heads().empty()) {
				auto lid = choose_lock(configuration, gen);
				auto tid = por::event::thread_id_t{};
				auto lock = configuration.lock_heads().find(lid)->second;
				if(lock->kind() == por::event::event_kind::lock_acquire) {
					if(configuration.thread_heads().find(lock->tid())->second->kind() != por::event::event_kind::thread_exit) {
						tid = lock->tid();
						configuration.destroy_lock(tid, lid);
						std::cout << "-L " << lid << " (" << tid << ")\n";
					}	else {
						// nop: lock is being held by a dead thread, and thus cannot be destroyed
					}
				} else {
					tid = choose_thread(configuration, gen);
					configuration.destroy_lock(tid, lid);
					std::cout << "-L " << lid << " (" << tid << ")\n";
				}
			}
		} else if(roll < 600) {
			// acquire lock, if one can be acquired
			for(bool done = false; !done; ) {
				unsigned count = 0;
				for(auto const& l : configuration.lock_heads()) {
					if(l.second->kind() == por::event::event_kind::lock_create || l.second->kind() == por::event::event_kind::lock_release) {
						++count;
						if(rare_choice(gen)) {
							auto tid = choose_thread(configuration, gen);
							configuration.acquire_lock(tid, l.first);
							std::cout << " L+ " << l.first << " (" << tid << ")\n";
							done = true;
							break;
						}
					}
				}
				if(count == 0) {
					// no acquirable locks exist
					done = true;
				}
			}
		} else if(roll < 900) {
			// release lock, if one can be released
			for(bool done = false; !done; ) {
				unsigned count = 0;
				for(auto const& l : configuration.lock_heads()) {
					if(l.second->kind() == por::event::event_kind::lock_acquire && configuration.thread_heads().find(l.second->tid())->second->kind() != por::event::event_kind::thread_exit) {
						++count;
						if(rare_choice(gen)) {
							auto const tid = l.second->tid();
							configuration.release_lock(tid, l.first);
							std::cout << " L- " << l.first << " (" << tid << ")\n";
							done = true;
							break;
						}
					}
				}
				if(count == 0) {
					// no releasable locks exist
					done = true;
				}
			}
		} else if(roll < 1000) {
			auto tid = choose_thread(configuration, gen);
			configuration.local(tid);
			std::cout << " . (" << tid << ")\n";
		} else {
			assert(false && "Unexpected random choice for event to introduce");
			std::abort();
		}
	}

	std::set<por::event::event*> visited;
	std::vector<por::event::event*> open;
	std::map<por::event::thread_id_t, std::vector<por::event::event*>> threads;
	std::vector<std::pair<por::event::event*, por::event::event*>> inter_thread_dependencies;
	std::map<por::event::thread_id_t, std::vector<std::pair<por::event::event*, por::event::event*>>> intra_thread_dependencies;
	std::size_t opened = 0;
	for(auto& t : configuration.thread_heads()) {
		open.push_back(t.second.get());
		++opened;
	}
	while(!open.empty()) {
		por::event::event* event = open.back();
		open.pop_back();
		if (!visited.insert(event).second) {
			// already visited
			continue;
		}
		por::event::thread_id_t tid = event->tid();
		threads[tid].push_back(event);
		bool first = true;
		for(auto& p : event->predecessors()) {
			por::event::event* predecessor = p.get();
			if(visited.count(predecessor) == 0) {
				open.push_back(predecessor);
				++opened;
			}
			if(tid != predecessor->tid()) {
				inter_thread_dependencies.emplace_back(std::make_pair(event, predecessor));
			} else if(first) {
				intra_thread_dependencies[tid].emplace_back(std::make_pair(event, predecessor));
				first = false; // only insert thread_predecessor, not lock or cond predecessor on same thread
			}
		}
	}

	std::cout << "\n\n";
	std::cout << "digraph {\n"
	          << "  rankdir=LR;\n"
	          << "  nodesep=0.8;\n"
	          << "  ranksep=1.6;\n";

	std::size_t event_id = 1;
	std::map<por::event::event const*, std::size_t> events;
	for(auto const& t : threads) {
		por::event::thread_id_t tid = t.first;
		if (tid == 0) {
			std::cout << "  subgraph cluster_T" << tid << " {\n"
			          << "    graph[style=invis]\n"
			          << "    node[shape=box style=dashed fixedsize=false width=1 label=\"\"]\n"
			          << "    {\n";

		} else {
			std::cout << "  subgraph cluster_T" << tid << " {\n"
			          << "    graph[label=\"Thread " << tid << "\"]\n"
			          << "    node[shape=box fixedsize=false width=1 label=\"\"]\n"
			          << "    edge[color=grey arrowhead=none]\n"
			          << "    {\n"
			          << "      rank=same;\n";
		}

		// topological sort of thread's events
		auto const& relation = intra_thread_dependencies[tid];
		std::vector<por::event::event const*> thread_events;
		auto _init = std::find_if(threads[tid].begin(), threads[tid].end(), [](por::event::event* e) {
			return e->kind() == por::event::event_kind::thread_init || e->kind() == por::event::event_kind::program_init;
		});
		assert(_init != threads[tid].end() && "each thread should have an init event");
		auto head = *_init;
		while(head != nullptr) {
			thread_events.push_back(head);
			auto it = std::find_if(relation.begin(), relation.end(), [&](auto const& edge) {
				return edge.second == head;
			});
			head = (it != relation.end()) ? it->first : nullptr;
		}

		std::size_t first_id = event_id;
		for(auto* e : thread_events) {
			events[e] = event_id;
			std::cout << "      e" << event_id << " [label=\"";
			switch(e->kind()) {
				case por::event::event_kind::program_init:
					std::cout << "init";
					break;
				case por::event::event_kind::local:
					std::cout << "loc";
					break;
				case por::event::event_kind::thread_init:
					std::cout << "+T";
					break;
				case por::event::event_kind::thread_exit:
					std::cout << "-T";
					break;
				case por::event::event_kind::thread_create:
					std::cout << "create";
					break;
				case por::event::event_kind::lock_create:
					std::cout << "+L";
					break;
				case por::event::event_kind::lock_destroy:
					std::cout << "-L";
					break;
				case por::event::event_kind::lock_acquire:
					std::cout << "acq";
					break;
				case por::event::event_kind::lock_release:
					std::cout << "rel";
					break;
			}

			std::cout << "\"]\n";
			++event_id;
		}
		std::cout << "    }\n";

		// print thread relation: e{event_id - 1} -> ... -> e{first_id}
		if ((first_id + 1) != event_id) {
			std::cout << "      e" << event_id - 1 << " -> ";
			for (std::size_t i = event_id - 2; i >= first_id; --i) {
				std::cout << "e" << i;
				if (i != first_id) {
					std::cout << " -> ";
				}
			}
		}
		std::cout << "\n"
		          << "  }\n";
	}
	std::cout << "  edge[arrowhead=vee constraint=false dir=back]\n"
	          << "\n";

	for(auto &r : inter_thread_dependencies) {
		std::cout << "  e" << events[r.second] << " -> e" << events[r.first];
		if(r.first->kind() == por::event::event_kind::thread_init) {
			//assert(r.second->kind() == por::event::thread_create);
			std::cout << " [constraint=true]";
		}
		std::cout << "\n";
	}

	std::cout << "}\n";
}
