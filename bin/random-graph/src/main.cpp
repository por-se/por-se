#include <por/program.h>

#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <random>
#include <iostream>
#include <iomanip>

namespace {
	por::event::thread_id_t choose_thread(por::program const& program, std::mt19937_64& gen) {
		std::uniform_int_distribution<std::size_t> dis(1, program.active_threads());
		std::size_t chosen = dis(gen);
		for(std::size_t i = 1, c = 0; i < program.thread_heads().size(); ++i){
			if(program.thread_heads()[i]->kind() != por::event::event_kind::thread_stop) {
				++c;
				if(c == chosen) {
					assert(i == program.thread_heads()[i]->tid());
					return i;
				}
			}
		}
		assert(false && "Active thread number was too large");
	}

	por::event::lock_id_t choose_lock(por::program const& program, std::mt19937_64& gen) {
		assert(!program.lock_heads().empty());

		std::uniform_int_distribution<std::size_t> dis(0, program.lock_heads().size() - 1);
		std::size_t chosen = dis(gen);
		return std::next(program.lock_heads().begin(), chosen)->first;
	}
}

int main(int argc, char** argv){
	assert(argc > 0);

	por::program program;
	{
		auto tid = program.spawn_thread(0);
		std::cout << "+T" << tid << " (0)\n";
	}

	std::mt19937_64 gen(42);
	// "warm up" mersenne twister to deal with weak initialization function
	for(unsigned i = 0; i < 10'000; ++i) {
		static_cast<void>(gen());
	}
	std::uniform_int_distribution<unsigned> event_dis(0, 999);
	std::bernoulli_distribution rare_choice(0.1);

	while(program.active_threads() > 0) {
		auto const roll = event_dis(gen);
		std::cout << "   r " << std::setw(3) << roll << "\n";
		if(roll < 40) {
			// spawn new thread
			auto source = choose_thread(program, gen);
			auto tid = program.spawn_thread(source);
			std::cout << "+T " << tid << " (" << source << ")\n";
		} else if(roll < 100) {
			// kill old thread
			auto tid = choose_thread(program, gen);
			program.stop_thread(tid);
			std::cout << "-T " << tid << "\n";
		} else if(roll < 200) {
			// spawn new lock
			auto tid = choose_thread(program, gen);
			auto lid = program.create_lock(tid);
			std::cout << "+L " << lid << " (" << tid << ")\n";
		} else if(roll < 300) {
			// destroy lock, if one exists
			if(!program.lock_heads().empty()) {
				auto lid = choose_lock(program, gen);
				auto tid = por::event::thread_id_t{};
				if(program.lock_heads().find(lid)->second->kind() == por::event::event_kind::lock_acquire) {
					tid = program.lock_heads().find(lid)->second->tid();
				} else {
					tid = choose_thread(program, gen);
				}
				program.destroy_lock(tid, lid);
				std::cout << "-L " << lid << " (" << tid << ")\n";
			}
		} else if(roll < 600) {
			// acquire lock, if one can be acquired
			for(bool done = false; !done; ) {
				unsigned count = 0;
				for(auto const& l : program.lock_heads()) {
					if(l.second->kind() == por::event::event_kind::lock_create || l.second->kind() == por::event::event_kind::lock_release) {
						++count;
						if(rare_choice(gen)) {
							auto tid = choose_thread(program, gen);
							program.acquire_lock(tid, l.first);
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
				for(auto const& l : program.lock_heads()) {
					if(l.second->kind() == por::event::event_kind::lock_acquire && program.thread_heads()[l.second->tid()]->kind() != por::event::event_kind::thread_stop) {
						++count;
						if(rare_choice(gen)) {
							auto const tid = l.second->tid();
							program.release_lock(tid, l.first);
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
			auto tid = choose_thread(program, gen);
			program.local(tid);
			std::cout << " . (" << tid << ")\n";
		} else {
			assert(false && "Unexpected random choice for event to introduce");
			std::abort();
		}
	}
}
