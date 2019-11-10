#include "include/por/node.h"

#include "include/por/configuration.h"
#include "include/por/event/event.h"

#include <algorithm>
#include <cassert>

using namespace por;

node* node::make_left_child(std::function<registration_t(por::configuration&)> func) {
	assert(!_left && "node already has left child");
	assert(!_event && "node must not have an event yet");

	_left = allocate_left_child();
	std::tie(_event, _left->_standby_state) = func(*_left->_C);

	// propagate sweep bit
	_left->_is_sweep_node = _is_sweep_node;
	_is_sweep_node = false;

	return _left.get();
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
	assert(_C->needs_catch_up() && _catch_up_ptr != nullptr);
	[[maybe_unused]] auto next = _C->peek();
	auto [event, standby_state] = func(*_C);
	assert(next == event);
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
		assert(n->_C->_catch_up.front() == event);
		new_C = std::make_shared<por::configuration>(*new_C);
		new_C->_catch_up.pop_back();
		n->_C = new_C;
		n->_standby_state = standby_state;
	} while(n != _catch_up_ptr || new_C->_catch_up.size() > 0);
	if(!n->_C->needs_catch_up() || n->_C->peek() != _C->peek()) {
		_catch_up_ptr = n->left_child();
	}
}

node* node::make_right_branch(por::comb A) {
	assert(_event && "no event attached to node");

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

		assert(n->_event != nullptr);
		por::configuration const& cfg = n->configuration();
		auto D = n->_D;
		D.push_back(n->_event);
		por::event::event const* j = cfg.compute_alternative(std::move(D));

		if(j == nullptr) {
			continue;
		}

		// compute A := [j] \setminus _C
		auto const& C = cfg.thread_heads();
		por::cone J = j->cone();
		J.insert(*j);

		por::comb A;
		for(auto& [tid, e] : J) {
			por::event::event const* t = e;
			if(C.count(tid) == 0) {
				// all events in [j] with same tid as e are not in C
				do {
					A.insert(*t);
					t = t->thread_predecessor();
				} while(t != nullptr);
			} else {
				por::event::event const* x = C.at(tid);
				while(x->depth() < t->depth()) {
					A.insert(*t);
					t = t->thread_predecessor();
				}
			}
		}
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
