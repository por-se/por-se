#include "include/por/node.h"

#include "include/por/configuration.h"
#include "include/por/event/event.h"

#include "util/check.h"

#include <algorithm>
#include <cassert>
#include <sstream>

using namespace por;

node* node::make_left_child(std::function<registration_t(por::configuration&)> func) {
	assert(!_left && "node already has left child");
	assert(!_event && "node must not have an event yet");

	_left = allocate_left_child();
	std::tie(_event, _left->_standby_state) = func(*_left->_C);

	libpor_check(std::find(_D.begin(), _D.end(), _event) == _D.end());

	// propagate sweep bit
	_left->_is_sweep_node = _is_sweep_node;
	_is_sweep_node = false;

	return _left.get();
}

node* node::make_right_local_child(std::function<registration_t(por::configuration&)> func) {
	assert(_event);
	assert(_event->kind() == por::event::event_kind::local);

	node* n = make_right_child();
	return n->make_left_child(std::move(func));
}

node* node::make_right_child() {
	assert(!_right && "node already has right child");
	assert(_event && "no event attached to node");

	auto D = _D;
	D.emplace_back(_event);
	_right = allocate_right_child(D);

	_right->_standby_state = _standby_state;

	return _right.get();
}

void node::catch_up(std::function<node::registration_t(por::configuration&)> func) {
	assert(_C->needs_catch_up());
	[[maybe_unused]] auto next = _C->peek();
	auto [event, standby_state] = func(*_C);
	assert(next == event);

	libpor_check(std::find(_D.begin(), _D.end(), event) == _D.end());

	if(standby_state) {
		_standby_state = standby_state;
	}

	if(_C->_catch_up.size() == 0) {
		_catch_up_ptr = nullptr;
	}

	if(!_catch_up_ptr || !standby_state) {
		// cannot update any other nodes
		return;
	}

	// update configurations and standby states of previous nodes from the same branch
	std::shared_ptr<por::configuration> new_C = _C;
	node* n = this;
	do {
		n = n->parent();
		assert(n != nullptr);
		auto& cu = n->_C->_catch_up;
		if(std::find(cu.begin(), cu.end(), event) == cu.end()) {
			break;
		}
		new_C = std::make_shared<por::configuration>(*new_C);
		new_C->_catch_up.pop_back();
		n->_C = new_C;
		n->_standby_state = standby_state;
	} while(n != _catch_up_ptr && new_C->_catch_up.size() > 0);
	if(!n->_C->needs_catch_up() || n->_C->peek() != _C->peek()) {
		_catch_up_ptr = n->left_child();
		_catch_up_ptr->_catch_up_ptr = nullptr;
		for(node* c = parent(); c != _catch_up_ptr && c; c = c->parent()) {
			n->_catch_up_ptr = _catch_up_ptr;
		}
	}
}

node* node::make_right_branch(por::comb A) {
	assert(_event && "no event attached to node");

	// FIXME: root node includes configuration with both program_init and thread_init event
	assert(parent() && "cannot be called on root node");

	node* n = make_right_child();
	node* catch_up_ptr = nullptr;

	if(!n->_standby_state) {
		// search for closest node with standby state in current branch
		node const* s = this;
		std::deque<por::event::event const*> catch_up_to_standby;
		while(!s->_standby_state && s->parent()) {
			s = s->parent();
			catch_up_to_standby.push_front(s->_event);
		}
		n->_standby_state = s->_standby_state;
		n->_C = std::make_shared<por::configuration>(*s->_C);
		n->_C->_catch_up.insert(n->_C->_catch_up.end(), catch_up_to_standby.begin(), catch_up_to_standby.end());
		catch_up_ptr = n;
	}

	while(!A.empty()) {
		A.sort();
		auto min = A.min();

		for(por::event::event const* a : min) {
			n = n->make_left_child([&a, &n](por::configuration&) {
				return std::make_pair(a, n->_standby_state);
			});
			assert(n->parent()->_event == a);
			n->_C->_catch_up.push_back(a);
			n->_catch_up_ptr = catch_up_ptr;
			if(!catch_up_ptr) {
				catch_up_ptr = n;
			}
		}

		A.remove(min.begin(), min.end());
	}

	return n;
}

std::vector<por::node*> node::create_right_branches(std::vector<por::node*> B) {
	std::vector<por::node*> leaves;
	for(auto n : B) {
		if(n->right_child()) {
			continue;
		}
		if(n->_event->kind() == por::event::event_kind::local) {
			// do not compute alternatives to local events, this is done by KLEE
			continue;
		}
		if(n->_event->kind() == por::event::event_kind::thread_init) {
			// thread_init always has to immediately succeed its thread_create
			// there is no (unique) alternative to be found here
			continue;
		}

		assert(n->_event != nullptr);
		por::configuration const& cfg = n->configuration();
		auto D = n->_D;
		D.push_back(n->_event);
		por::event::event const* j = cfg.compute_alternative(std::move(D));

		if(j == nullptr) {
			continue;
		}

		// compute A := [j] \setminus _C
		por::cone J(*j);
		por::cone C(cfg);
		por::comb A = J.setminus(C);

		if(n->needs_catch_up()) {
			for(auto& u : n->_C->_catch_up) {
				A.remove(*u);
			}
		}
		libpor_check(A.is_sorted());

		leaves.push_back(n->make_right_branch(std::move(A)));
	}
	return leaves;
}

void node::update_sweep_bit() {
	assert(_left == nullptr && _right != nullptr);
	node* n = right_child();
	while(n->left_child()) {
		n = n->left_child();
	}
	n->_is_sweep_node = true;
	assert(!_is_sweep_node);
}

void node::backtrack() {
	assert(!has_children());
	if(!_is_sweep_node) {
		return;
	}
	node* n = this;
	while(n && !n->has_children()) {
		node* p = n->parent();

		// remove n
		if(p && p->left_child() == n) {
			p->_left.reset();
		} else if(p && p->right_child() == n) {
			p->_right.reset();
		}

		n = p;
	}
	if(!n) {
		return;
	}
	n->update_sweep_bit();
}

bool node::needs_catch_up() const noexcept {
	assert(_C);
	return _C->needs_catch_up();
}

por::event::event const* node::peek() const noexcept {
	assert(_C);
	if(!needs_catch_up()) {
		return nullptr;
	}
	return _C->peek();
}

std::string node::to_string(bool with_schedule) const noexcept {
	std::ostringstream result;
	result << "node " << this << "\n";

	result << "branch:\n";
	std::vector<node const*> br(branch_begin(), branch_end());
	std::reverse(br.begin(), br.end());
	for(node const* n : br) {
		result << "  node: " << n << "\n";
		result << "    parent: ";
		if(!n->parent()) {
			result << "nullptr\n";
		} else {
			result << n->parent();
			if(n->parent()->left_child() == n) {
				result << " (left child)\n";
			} else if(n->parent()->right_child() == n) {
				result << " (right child)\n";
			}
		}
		result << "    event: ";
		if(!n->_event) {
			result << "nullptr\n";
		} else {
			result << n->_event->to_string(true) << " @ " << n->_event << "\n";
		}
		result << "    standby_state: ";
		if(!n->_standby_state) {
			result << "nullptr\n";
		} else {
			result << n->_standby_state << "\n";
		}
		result << "    catch_up_ptr: ";
		if(!n->_catch_up_ptr) {
			result << "nullptr\n";
		} else {
			result << n->_catch_up_ptr << "\n";
		}
		result << "    is_sweep_node: " << n->_is_sweep_node << "\n";
		result << "    |C| = " << std::to_string(n->configuration().size());
		if(n->configuration().needs_catch_up()) {
			result << " (+ " << n->configuration()._catch_up.size() << " catch-up)";
			for(auto x : n->configuration()._catch_up) {
				result << "\n      catch-up: " <<  x->to_string(true) << " @ " << x;
			}
		}
		result << "\n";
		result << "    |D| = " << std::to_string(n->_D.size()) << "\n";
		for(auto& d : n->_D) {
			result << "      " << d->to_string(true) << " @ " << d << "\n";
		}
	}

	if(with_schedule) {
		auto sched = schedule();
		if(!sched.empty()) {
			result << "schedule:\n";
			for(auto* e : sched) {
				assert(e);
				result << "  " << e->to_string(true) << " @ " << e << "\n";
			}
		}
	}

	return result.str();
}
