#include "include/por/configuration.h"

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

void configuration::to_dotgraph(std::ostream& os) const noexcept {
	using event_relation_t = std::pair<por::event::event const*, por::event::event const*>;

	std::vector<event_relation_t> inter_thread_dependencies;
	std::map<por::event::thread_id_t, std::vector<event_relation_t>> non_immediate_intra_thread_dependencies;

	for(por::event::event const* event : *this) {
		por::event::thread_id_t const& tid = event->tid();
		por::event::event const* thread_pred = event->thread_predecessor();
		bool first = true;
		for(por::event::event const* predecessor : event->predecessors()) {
			if(tid != predecessor->tid()) {
				inter_thread_dependencies.emplace_back(std::make_pair(event, predecessor));
			} else {
				if(first) {
					first = false;
					continue;
				}
				non_immediate_intra_thread_dependencies[tid].emplace_back(std::make_pair(event, predecessor));
			}
		}
	}

	os << "digraph {\n"
	   << "  rankdir=TB;\n";

	std::size_t event_id = 1;
	std::map<por::event::event const*, std::size_t> events;

	por::event::event const* program_init = &_unfolding->root();

	os << "\n"
	   << "  subgraph \"cluster_T0\" {\n"
	   << "    graph[style=invis]\n\n"
	   << "    node[shape=box style=dashed fixedsize=false width=1 label=\"\"]\n"
	   << "    // single visible node\n"
	   << "    e" << event_id << " [label=\"init depth=" << program_init->depth() << "\"]\n"
	   << "  }\n";

	events[program_init] = event_id++;


	for(auto const& [tid, head] : thread_heads()) {
		os << "\n"
		  << "  subgraph \"cluster_T" << tid << "\" {\n"
		  << "    graph[label=\"Thread " << tid << "\"]\n\n"
		  << "    node[shape=box fixedsize=false width=1 label=\"\"]\n"
		  << "    // visible and invisible nodes\n";

		std::deque<por::event::event const*> thread_events;
		{
			por::event::event const* e = head;
			do {
				thread_events.push_front(e);
				e = e->thread_predecessor();
			} while(e);
		}

		std::size_t first_id = event_id;
		std::size_t depth = thread_events.front()->depth();
		std::vector<std::size_t> visibleNodes;
		for(auto* e : thread_events) {
			// account for difference in depth by inserting a number of invisible nodes
			for(std::size_t i = depth; (i + 1) < e->depth(); i++) {
				os << "    e" << event_id++ << " [style=invis width=0 height=0]\n";
			}

			events[e] = event_id;
			visibleNodes.push_back(event_id);
			depth = e->depth();

			os << "    e" << event_id << " [label=\"";
			switch(e->kind()) {
				case por::event::event_kind::local:
					os << "loc";
					break;
				case por::event::event_kind::thread_init:
					os << "+T";
					break;
				case por::event::event_kind::thread_join:
					os << "join";
					break;
				case por::event::event_kind::thread_exit:
					os << "-T";
					break;
				case por::event::event_kind::thread_create:
					os << "create";
					break;
				case por::event::event_kind::lock_create:
					os << "+L";
					break;
				case por::event::event_kind::lock_destroy:
					os << "-L";
					break;
				case por::event::event_kind::lock_acquire:
					os << "acq";
					break;
				case por::event::event_kind::lock_release:
					os << "rel";
					break;
				case por::event::event_kind::condition_variable_create:
					os << "+C";
					break;
				case por::event::event_kind::condition_variable_destroy:
					os << "-C";
					break;
				case por::event::event_kind::wait1:
					os << "w1";
					break;
				case por::event::event_kind::wait2:
					os << "w2";
					break;
				case por::event::event_kind::signal:
					os << "sig";
					break;
				case por::event::event_kind::broadcast:
					os << "bro";
					break;
				default:
					assert(0 && "unhandled event_kind!");
			}

			os << " depth=" << e->depth();

			if(e->kind() == por::event::event_kind::local) {
				auto l = static_cast<por::event::local const*>(e);
				if(!l->path().empty()) {
					os << " path=";
					for(auto b : l->path()) {
						os << std::to_string(b);
					}
				}
			}

			os << "\"]\n";
			++event_id;
		}

		std::vector<event_relation_t> deps = non_immediate_intra_thread_dependencies[tid];

		if(visibleNodes.size() > 1) {
			os << "\n"
			   << "    edge[color=grey arrowtail=vee weight=1000 dir=back]\n"
			   << "    // visible edges\n";
			auto prev_it = visibleNodes.begin();
			for(auto it = visibleNodes.begin() + 1, ie = visibleNodes.end(); it != ie; ++it) {
				os << "    e" << *prev_it << " -> e" << *it;
				por::event::event const* prev_event = thread_events[std::distance(visibleNodes.begin(), prev_it)];
				por::event::event const* curr_event = thread_events[std::distance(visibleNodes.begin(), it)];
				auto deps_it = std::find(deps.begin(), deps.end(), std::make_pair(curr_event, prev_event));
				if(deps_it != deps.end()) {
					os << "[color=blue]";
					deps.erase(deps_it);
				}
				os << "\n";
				prev_it = it;
			}
		}

		if((event_id - first_id) > visibleNodes.size()) {
			os << "\n"
			   << "    edge[style=invis]\n"
			   << "    // invisible edges\n";
			auto prev_it = visibleNodes.begin();
			for(auto it = visibleNodes.begin() + 1, ie = visibleNodes.end(); it != ie; ++it) {
				bool gap = *it - *prev_it > 1;
				for(std::size_t i = *prev_it; gap && i <= (*it - 1); ++i) {
					os << "    e" << i << " -> e" << i + 1 << "\n";
				}
				prev_it = it;
			}
		}

		if(deps.size() > 0) {
			os << "\n"
			   << "    edge[color=blue arrowtail=vee style=dashed dir=back constraint=false weight=0]\n"
			   << "    // object dependency edges\n";
			for(auto &r : deps) {
				os << "    e" << events[r.second] << " -> e" << events[r.first] << "\n";
			}
		}

		os << "  }\n";
	}
	os << "  edge[color=blue arrowtail=vee dir=back]\n"
	   << "  // dependency edges across threads\n";

	for(auto &r : inter_thread_dependencies) {
		os << "  e" << events[r.second] << " -> e" << events[r.first] << "\n";
	}

	os << "}\n";
}
