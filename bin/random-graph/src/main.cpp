#include <por/program.h>

#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <random>
#include <iostream>
#include <iomanip>

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
}
