#include "include/por/configuration.h"

#include <iostream>

using namespace por;

configuration_iterator::configuration_iterator(por::configuration const& configuration, bool end) {
	_configuration = &configuration;
	if(!end && !configuration.thread_heads().empty()) {
		_thread = configuration.thread_heads().rbegin();
		_event = _thread->second;
	}
}

configuration_iterator& configuration_iterator::operator++() noexcept {
	if(!_event) {
		return *this;
	}

	if(por::event::event const* p = _event->thread_predecessor()) {
		_event = p;
	} else if(_thread != std::prev(_configuration->thread_heads().rend())) {
		++_thread;
		_event = _thread->second;
	} else if(_event == &_configuration->unfolding()->root()) {
		_event = nullptr;
		_thread = decltype(_thread)();
	} else {
		assert(std::next(_thread) == _configuration->thread_heads().rend());
		_event = &_configuration->unfolding()->root();
	}
	return *this;
}

void configuration::to_dotgraph() const noexcept {
	std::set<por::event::event const*> visited;
	std::vector<por::event::event const*> open;
	std::map<por::event::thread_id_t, std::vector<por::event::event const*>> threads;
	std::vector<std::pair<por::event::event const*, por::event::event const*>> inter_thread_dependencies;
	std::map<por::event::thread_id_t, std::vector<std::pair<por::event::event const*, por::event::event const*>>> non_immediate_intra_thread_dependencies;
	std::map<por::event::thread_id_t, std::vector<std::pair<por::event::event const*, por::event::event const*>>> intra_thread_dependencies;

	for(auto& t : thread_heads()) {
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
		bool first = true;
		for(auto& p : event->predecessors()) {
			por::event::event const* predecessor = p;
			if(visited.count(predecessor) == 0) {
				open.push_back(predecessor);
			}
			if(tid != predecessor->tid()) {
				inter_thread_dependencies.emplace_back(std::make_pair(event, predecessor));
			} else {
				if(first) {
					intra_thread_dependencies[tid].emplace_back(std::make_pair(event, predecessor));
					first = false; // only insert thread_predecessor, not lock or cond predecessor on same thread
				} else {
					non_immediate_intra_thread_dependencies[tid].emplace_back(std::make_pair(event, predecessor));
				}
			}
		}
	}

	std::cout << "digraph {\n"
	          << "  rankdir=TB;\n";

	std::size_t event_id = 1;
	std::map<por::event::event const*, std::size_t> events;
	for(auto const& t : threads) {
		por::event::thread_id_t tid = t.first;
		if(!tid) {
			std::cout << "\n"
			          << "  subgraph \"cluster_T0\" {\n"
			          << "    graph[style=invis]\n\n"
			          << "    node[shape=box style=dashed fixedsize=false width=1 label=\"\"]\n"
			          << "    // single visible node\n";

		} else {
			std::cout << "\n"
			          << "  subgraph \"cluster_T" << tid << "\" {\n"
			          << "    graph[label=\"Thread " << tid << "\"]\n\n"
			          << "    node[shape=box fixedsize=false width=1 label=\"\"]\n"
			          << "    // visible and invisible nodes\n";
		}

		// topological sort of thread's events
		auto const& relation = intra_thread_dependencies[tid];
		std::vector<por::event::event const*> thread_events;
		auto _init = std::find_if(threads[tid].begin(), threads[tid].end(), [](por::event::event const* e) {
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
		std::size_t depth = (*_init)->depth();
		std::vector<std::size_t> visibleNodes;
		for(auto* e : thread_events) {
			// account for difference in depth by inserting a number of invisible nodes
			for(std::size_t i = depth; (i + 1) < e->depth(); i++) {
				std::cout << "    e" << event_id++ << " [style=invis width=0 height=0]\n";
			}

			events[e] = event_id;
			visibleNodes.push_back(event_id);
			depth = e->depth();

			std::cout << "    e" << event_id << " [label=\"";
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
				case por::event::event_kind::thread_join:
					std::cout << "join";
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
				case por::event::event_kind::condition_variable_create:
					std::cout << "+C";
					break;
				case por::event::event_kind::condition_variable_destroy:
					std::cout << "-C";
					break;
				case por::event::event_kind::wait1:
					std::cout << "w1";
					break;
				case por::event::event_kind::wait2:
					std::cout << "w2";
					break;
				case por::event::event_kind::signal:
					std::cout << "sig";
					break;
				case por::event::event_kind::broadcast:
					std::cout << "bro";
					break;
			}

			std::cout << " depth=" << e->depth();
			std::cout << "\"]\n";
			++event_id;
		}

		std::vector<std::pair<por::event::event const*, por::event::event const*>> deps = non_immediate_intra_thread_dependencies[tid];

		if(visibleNodes.size() > 1) {
			std::cout << "\n"
			          << "    edge[color=grey arrowtail=vee weight=1000 dir=back]\n"
			          << "    // visible edges\n";
			auto prev_it = visibleNodes.begin();
			for(auto it = visibleNodes.begin() + 1, ie = visibleNodes.end(); it != ie; ++it) {
				std::cout << "    e" << *prev_it << " -> e" << *it;
				por::event::event const* prev_event = thread_events[std::distance(visibleNodes.begin(), prev_it)];
				por::event::event const* curr_event = thread_events[std::distance(visibleNodes.begin(), it)];
				auto deps_it = std::find(deps.begin(), deps.end(), std::make_pair(curr_event, prev_event));
				if(deps_it != deps.end()) {
					std::cout << "[color=blue]";
					deps.erase(deps_it);
				}
				std::cout << "\n";
				prev_it = it;
			}
		}

		if((event_id - first_id) > visibleNodes.size()) {
			std::cout << "\n"
			          << "    edge[style=invis]\n"
			          << "    // invisible edges\n";
			auto prev_it = visibleNodes.begin();
			for(auto it = visibleNodes.begin() + 1, ie = visibleNodes.end(); it != ie; ++it) {
				bool gap = *it - *prev_it > 1;
				for(std::size_t i = *prev_it; gap && i <= (*it - 1); ++i) {
					std::cout << "    e" << i << " -> e" << i + 1 << "\n";
				}
				prev_it = it;
			}
		}

		if(deps.size() > 0) {
			std::cout << "\n"
			          << "    edge[color=blue arrowtail=vee style=dashed dir=back constraint=false weight=0]\n"
			          << "    // object dependency edges\n";
			for(auto &r : deps) {
				std::cout << "    e" << events[r.second] << " -> e" << events[r.first] << "\n";
			}
		}

		std::cout << "  }\n";
	}
	std::cout << "  edge[color=blue arrowtail=vee dir=back]\n"
	          << "  // dependency edges across threads\n";

	for(auto &r : inter_thread_dependencies) {
		std::cout << "  e" << events[r.second] << " -> e" << events[r.first] << "\n";
	}

	std::cout << "}\n";
}
