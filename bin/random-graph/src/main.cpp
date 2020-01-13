#include <por/configuration.h>
#include <por/thread_id.h>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <random>
#include <iostream>
#include <iomanip>
#include <set>
#include <sstream>

#include <util/sso_array.h>

namespace {
	por::event::thread_id_t choose_thread(por::configuration const& configuration, std::mt19937_64& gen) {
		std::uniform_int_distribution<std::size_t> dis(1, configuration.active_threads());
		std::size_t const chosen = dis(gen);
		std::size_t count = 0;
		for(auto it = configuration.thread_heads().begin(); ; ++it){
			assert(it != configuration.thread_heads().end());
			if(it->second->kind() != por::event::event_kind::thread_exit && it->second->kind() != por::event::event_kind::wait1) {
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

	por::event::thread_id_t choose_suitable_thread(por::configuration const& configuration,
		std::mt19937_64& gen,
		std::bernoulli_distribution& rare_choice,
		por::event::event_kind kind
	) {
		for(bool done = false; !done; ) {
			unsigned count = 0;
			for(auto const& t : configuration.thread_heads()) {
				if(t.second->kind() != kind)
					continue;

				++count;
				if(rare_choice(gen)) {
					return t.first;
				}
			}
			if(count == 0) {
				// no suitable threads exist
				done = true;
			}
		}
		return {};
	}

	por::event::lock_id_t choose_suitable_lock(por::configuration const& configuration,
		std::mt19937_64& gen,
		std::bernoulli_distribution& rare_choice,
		bool released,
		por::event::thread_id_t locked_by_tid = {}
	) {
		for(bool done = false; !done; ) {
			unsigned count = 0;
			for(auto const& l : configuration.lock_heads()) {
				auto lock_kind = l.second->kind();
				bool suitable = false;
				if(released) {
					suitable |= lock_kind == por::event::event_kind::lock_create;
					suitable |= lock_kind == por::event::event_kind::lock_release;
					suitable |= lock_kind == por::event::event_kind::wait1;
				} else {
					suitable |= lock_kind == por::event::event_kind::lock_acquire;
					suitable |= lock_kind == por::event::event_kind::wait2;
					if(suitable && locked_by_tid)
						suitable = l.second->tid() == locked_by_tid;
					if(suitable)
						suitable = configuration.thread_heads().find(l.second->tid())->second->kind() != por::event::event_kind::thread_exit;
				}

				if(!suitable)
					continue;

				++count;
				if(rare_choice(gen)) {
					return l.first;
				}
			}
			if(count == 0) {
				// no suitable locks exist
				done = true;
			}
		}
		return 0;
	}

	por::event::cond_id_t choose_cond(por::configuration const& configuration, std::mt19937_64& gen) {
		if(configuration.cond_heads().empty())
			return 0;

		std::uniform_int_distribution<std::size_t> dis(0, configuration.cond_heads().size() - 1);
		std::size_t chosen = dis(gen);
		return std::next(configuration.cond_heads().begin(), chosen)->first;
	}

	por::event::cond_id_t choose_suitable_cond(por::configuration const& configuration,
		std::mt19937_64& gen,
		std::bernoulli_distribution& rare_choice,
		bool blocked
	) {
		for(bool done = false; !done; ) {
			unsigned count = 0;
			for(auto const& c : configuration.cond_heads()) {
				std::size_t num_blocked = std::count_if(c.second.begin(), c.second.end(), [](auto& e) { return e->kind() == por::event::event_kind::wait1; });
				if((!blocked && num_blocked > 0) || (blocked && num_blocked == 0))
					continue;

				++count;
				if(rare_choice(gen)) {
					return c.first;
				}
			}
			if(count == 0) {
				// no suitable conds exist
				done = true;
			}
		}
		return 0;
	}
}

template<>
std::string por::event::local<std::uint64_t>::path_string() const noexcept {
	std::stringstream ss;
	for(auto& p : path()) {
		ss << std::to_string(p);
	}
	return ss.str();
}

int main(int argc, char** argv){
	assert(argc > 0);

	por::configuration configuration; // construct a default configuration with 1 main thread
	por::event::lock_id_t next_lock_id = 1;
	por::event::cond_id_t next_cond_id = 1;

	// Count of threads that every thread has spawned
	std::map<por::event::thread_id_t, std::uint16_t> thread_spawns{};

#ifdef SEED
	std::mt19937_64 gen(SEED);
#else
	std::mt19937_64 gen(35);
#endif

	if(argc > 1) {
		gen.seed(std::stoi(argv[1]));
	}

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
			std::uint16_t local_id = ++thread_spawns[source];
			auto tid = por::thread_id(source, local_id);
			configuration.create_thread(source, tid);
			configuration.init_thread(tid, source);
			std::cout << "+T " << tid << " (" << source << ")\n";
		} else if(roll < 60) {
			// join thread
			auto tid = choose_thread(configuration, gen);
			auto join_tid = choose_suitable_thread(configuration, gen, rare_choice, por::event::event_kind::thread_exit);
			if(tid && join_tid) {
				configuration.join_thread(tid, join_tid);
				std::cout << "jT " << tid << " " << join_tid << "\n";
				break;
			}
		} else if(roll < 100) {
			// kill old thread
			auto tid = choose_thread(configuration, gen);
			configuration.exit_thread(tid);
			std::cout << "-T " << tid << "\n";
		} else if(roll < 200) {
			// spawn new lock
			auto tid = choose_thread(configuration, gen);
			auto lid = next_lock_id++;
			configuration.create_lock(tid, lid);
			std::cout << "+L " << lid << " (" << tid << ")\n";
		} else if(roll < 300) {
			// destroy lock, if one exists
			auto lid = choose_suitable_lock(configuration, gen, rare_choice, true);
			auto tid = choose_thread(configuration, gen);
			if(lid && tid) {
				bool no_block_on_lock = true;
				for(auto& e : configuration.thread_heads()) {
					if(e.second->kind() == por::event::event_kind::wait1) {
						auto& w = e.second;
						auto* l = configuration.lock_heads().at(lid);
						while(l != nullptr && w->is_less_than(*l)) {
							l = l->lock_predecessor();
						}
						if(l == w) {
							no_block_on_lock = false;
						}
					}
				}
				if(no_block_on_lock) {
					configuration.destroy_lock(tid, lid);
					std::cout << "-L " << lid << " (" << tid << ")\n";
				}
			}
		} else if(roll < 400) {
			// acquire lock, if one can be acquired
			auto lid = choose_suitable_lock(configuration, gen, rare_choice, true);
			auto tid = choose_thread(configuration, gen);
			if(lid && tid) {
				configuration.acquire_lock(tid, lid);
				std::cout << " L+ " << lid << " (" << tid << ")\n";
			}
		} else if(roll < 500) {
			// release lock, if one can be released
			auto lid = choose_suitable_lock(configuration, gen, rare_choice, false);
			if(lid) {
				auto const tid = configuration.lock_heads().find(lid)->second->tid();
				if(configuration.thread_heads().find(tid)->second->kind() != por::event::event_kind::wait1) {
					configuration.release_lock(tid, lid);
					std::cout << " L- " << lid << " (" << tid << ")\n";
				}
			}
		} else if(roll < 600) {
			// wait on condition variable, if possible
			auto tid = choose_thread(configuration, gen);
			auto lid = choose_suitable_lock(configuration, gen, rare_choice, false, tid);
			auto cid = choose_cond(configuration, gen);
			if(tid && lid && cid) {
				configuration.wait1(tid, cid, lid);
				std::cout << " C+ " << cid << ", " <<  lid << " (" << tid << ")\n";
			}
		} else if(roll < 700) {
			// signal single thread, if possible
			auto tid = choose_thread(configuration, gen);
			auto cid = choose_suitable_cond(configuration, gen, rare_choice, true);
			por::event::thread_id_t blocked_tid{};
			if(tid && cid) {
				for(auto& w : configuration.cond_heads().at(cid)) {
					if(w->kind() != por::event::event_kind::wait1 || w->tid() == tid)
						continue;
					blocked_tid = w->tid();
					break;
				}
				configuration.signal_thread(tid, cid, blocked_tid);
				std::cout << "sT " << cid << ", " <<  blocked_tid << " (" << tid << ")\n";
			}
		} else if(roll < 750) {
			// lost signal, if possible
			auto tid = choose_thread(configuration, gen);
			auto cid = choose_suitable_cond(configuration, gen, rare_choice, false);
			if(tid && cid) {
				configuration.signal_thread(tid, cid, {});
				std::cout << "sT " << cid << ", " <<  0 << " (" << tid << ")\n";
			}
		} else if(roll < 800) {
			// broadcast threads, if possible
			auto tid = choose_thread(configuration, gen);
			auto cid = choose_suitable_cond(configuration, gen, rare_choice, true);
			std::vector<por::event::thread_id_t> blocked_tids;
			if(tid && cid) {
				for(auto& w : configuration.cond_heads().at(cid)) {
					if(w->kind() != por::event::event_kind::wait1 || w->tid() == tid)
						continue;
					blocked_tids.push_back(w->tid());
					break;
				}
				configuration.broadcast_threads(tid, cid, blocked_tids);
				std::cout << "bT " << cid << ", " <<  blocked_tids.size() << " threads (" << tid << ")\n";
			}
		} else if(roll < 850) {
			// lost broadcast, if possible
			auto tid = choose_thread(configuration, gen);
			auto cid = choose_suitable_cond(configuration, gen, rare_choice, false);
			if(tid && cid) {
				configuration.broadcast_threads(tid, cid, {});
				std::cout << "bT " << cid << ", {} (" << tid << ")\n";
			}
		} else if(roll < 900) {
			// wake up notified thread, if possible
			auto tid = choose_suitable_thread(configuration, gen, rare_choice, por::event::event_kind::wait1);
			if(tid) {
				auto& wait1 = configuration.thread_heads().find(tid)->second;
				por::event::cond_id_t cid = 0;
				for(auto& cond : configuration.cond_heads()) {
					for(auto& e : cond.second) {
						if(e->tid() == tid || (e->kind() != por::event::event_kind::signal && e->kind() != por::event::event_kind::broadcast))
							continue;
						if(e->kind() == por::event::event_kind::signal) {
							auto sig = static_cast<por::event::signal const*>(e);
							if(sig->wait_predecessor() == wait1) {
								cid = cond.first;
							}
						} else {
							assert(e->kind() == por::event::event_kind::broadcast);
							auto bro = static_cast<por::event::broadcast const*>(e);
							for(auto& w : bro->wait_predecessors()) {
								if(w == wait1) {
									cid = cond.first;
									break;
								}
							}
						}
						if(cid)
							break;
					}
					if(cid)
						break;
				}
				if(cid) {
					auto lid = 0;
					for(auto& e : configuration.lock_heads()) {
						auto* l = e.second;
						while(l != nullptr && wait1->is_less_than(*l)) {
							l = l->lock_predecessor();
						}
						if(l == wait1) {
							lid = e.first;
							auto currently = e.second->kind();
							if(currently != por::event::event_kind::wait1 && currently != por::event::event_kind::lock_release)
								lid = 0;
						}
					}
					if(lid) {
						configuration.wait2(tid, cid, lid);
						std::cout << "wT " << cid << ", " <<  lid << " (" << tid << ")\n";
					}
				}
			}
		} else if(roll < 950) {
			// spawn new cond
			auto tid = choose_thread(configuration, gen);
			auto cid = next_cond_id++;
			configuration.create_cond(tid, cid);
			std::cout << "+C " << cid << " (" << tid << ")\n";
		} else if(roll < 970) {
			// destroy cond, if one exists
			auto tid = choose_thread(configuration, gen);
			auto cid = choose_suitable_cond(configuration, gen, rare_choice, false);
			if(cid) {
				configuration.destroy_cond(tid, cid);
				std::cout << "-C " << cid << " (" << tid << ")\n";
			}
		} else if(roll < 1000) {
			auto tid = choose_thread(configuration, gen);
			configuration.local<std::uint64_t>(tid, {});
			std::cout << " . (" << tid << ")\n";
		} else {
			assert(false && "Unexpected random choice for event to introduce");
			std::abort();
		}
	}

	auto cex = configuration.conflicting_extensions();
	std::cerr << cex.size() << " cex found\n";
	for(auto& entry : cex) {
		std::cerr << entry->to_string(true) << " @ " << entry << "\n";
		std::cerr << "with immediate predecessor(s):\n";
		for(auto e : entry->immediate_predecessors()) {
			std::cerr << "\t" << e->to_string(true) << " @ " << e << "\n";
		}
		std::cerr << "and immediate conflict(s):\n";
		auto icfl = entry->immediate_conflicts();
		for(auto e : icfl) {
			std::cerr << "\t" << e->to_string(true) << " @ " << e << "\n";
			auto e_icfl = e->immediate_conflicts();
			for(auto c : e_icfl) {
				assert(std::find(e_icfl.begin(), e_icfl.end(), entry) != icfl.end());
			}
		}
		std::cerr << "\n";
	}

	std::set<por::event::event const*> visited;
	std::vector<por::event::event const*> open;
	std::map<por::event::thread_id_t, std::vector<por::event::event const*>> threads;
	for(auto& t : configuration.thread_heads()) {
		open.push_back(t.second);
	}
	while(!open.empty()) {
		por::event::event const* event = open.back();
		open.pop_back();
		if(!visited.insert(event).second) {
			// already visited
			continue;
		}
		por::event::thread_id_t tid = event->tid();
		threads[tid].push_back(event);
		for(auto& p : event->predecessors()) {
			por::event::event const* predecessor = p;
			if(visited.count(predecessor) == 0) {
				open.push_back(predecessor);
			}
		}
	}

	assert(visited.size() == std::distance(configuration.begin(), configuration.end()));
	assert(visited.size() == configuration.size());

#ifndef NDEBUG
	std::set<por::event::event const*> E;
	for(auto& [tid, t] : configuration.thread_heads()) {
		for(auto& l : t->local_configuration()) {
			if(l) {
				E.insert(l);
			}
		}
		E.insert(t);
	}
	assert(E.size() == std::distance(configuration.begin(), configuration.end()));
	assert(E.size() == configuration.size());

	// check event_iterator for all events (and for all its options)
	for(auto&e : E) {
		auto& program_init = configuration.unfolding()->root();

		// compute causes(e) \setminus {program_init}
		por::comb causes_no_root;
		for(auto [tid, c] : e->cone()) {
			do {
				causes_no_root.insert(*c);
				c = c->thread_predecessor();
			} while(c);
		}
		assert(causes_no_root.is_sorted());

		// compute causes(e)
		por::comb causes = causes_no_root;
		if(e != &program_init) {
			causes.insert(program_init);
			assert(causes.size() == causes_no_root.size() + 1);
		} else {
			assert(causes.size() == 0);
			assert(causes.size() == causes_no_root.size());
		}

		// compute [e] \setminus {program_init}
		por::comb configuration_no_root = causes_no_root;
		configuration_no_root.insert(*e);
		assert(configuration_no_root.size() == causes_no_root.size() + 1);

		// compute [e]
		por::comb configuration = configuration_no_root;
		configuration.insert(program_init);
		if(e != &program_init) {
			assert(configuration.size() == configuration_no_root.size() + 1);
		} else {
			assert(configuration.size() == 1);
			assert(configuration.size() == configuration_no_root.size());
		}


		// with_root = false, with_event = false => ⌈e⌉ \ {program_init} (causes of e without root event)
		por::event::event_iterator causes_no_root_it(*e, false, false);
		por::event::event_iterator causes_no_root_ie(*e, false, false, true);

		assert(causes_no_root.size() == std::distance(causes_no_root_it, causes_no_root_ie));
		std::set<por::event::event const*> causes_no_root_set(causes_no_root.begin(), causes_no_root.end());
		std::set<por::event::event const*> causes_no_root_it_set(causes_no_root_it, causes_no_root_ie);
		assert(causes_no_root_set == causes_no_root_it_set);


		// with_root =  true, with_event = false => ⌈e⌉ := [e] \ {e} (causes of e)
		por::event::event_iterator causes_it(*e, true, false);
		por::event::event_iterator causes_ie(*e, true, false, true);

		assert(causes.size() == std::distance(causes_it, causes_ie));
		std::set<por::event::event const*> causes_set(causes.begin(), causes.end());
		std::set<por::event::event const*> causes_it_set(causes_it, causes_ie);
		assert(causes_set == causes_it_set);


		// with_root =  true, with_event =  true => [e] (local configuration of e)
		por::event::event_iterator configuration_it(*e, true, true);
		por::event::event_iterator configuration_ie(*e, true, true, true);

		assert(configuration.size() == std::distance(configuration_it, configuration_ie));
		std::set<por::event::event const*> configuration_set(configuration.begin(), configuration.end());
		std::set<por::event::event const*> configuration_it_set(configuration_it, configuration_ie);
		assert(configuration_set == configuration_it_set);


		// with_root = false, with_event =  true => [e] \ {program_init} (local configuration without root event)
		por::event::event_iterator configuration_no_root_it(*e, false, true);
		por::event::event_iterator configuration_no_root_ie(*e, false, true, true);

		assert(configuration_no_root.size() == std::distance(configuration_no_root_it, configuration_no_root_ie));
		std::set<por::event::event const*> configuration_no_root_set(configuration_no_root.begin(), configuration_no_root.end());
		std::set<por::event::event const*> configuration_no_root_it_set(configuration_no_root_it, configuration_no_root_ie);
		assert(configuration_no_root_set == configuration_no_root_it_set);
	}

#endif

#ifndef NDEBUG
	for(auto& v : visited) {
		for(auto& w : visited) {
			if(v->is_independent_of(w)) {
				if(!w->is_independent_of(v)) {
					std::cerr << "Symmetry failure:\n";
					std::cerr << v->to_string(true) << " IS independent of " << w->to_string(true) << "\n";
					std::cerr << "However: " << w->to_string(true) << " IS NOT independent of " << v->to_string(true) << "\n";
				}
				assert(w->is_independent_of(v));
			} else {
				if(w->is_independent_of(v)) {
					std::cerr << "Symmetry failure:\n";
					std::cerr << v->to_string(true) << " IS NOT independent of " << w->to_string(true) << "\n";
					std::cerr << "HOWEVER: " << w->to_string(true) << " IS independent of " << v->to_string(true) << "\n";
				}
				assert(!w->is_independent_of(v));
			}
		}
	}
#endif

	std::cout << "\n\n";
	configuration.to_dotgraph(std::cout);
}
