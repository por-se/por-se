#pragma once

#include "por/configuration.h"

#include <cassert>
#include <cstdint>
#include <deque>
#include <memory>
#include <stack>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#ifdef LIBPOR_KLEE
namespace klee {
	class ExecutionState;
}
#endif

namespace por {
	class configuration;
	class node;
	class unfolding;

#ifdef LIBPOR_KLEE
	using state = klee::ExecutionState;
#else
	struct state {};
#endif

	namespace event {
		class event;
	}

	using event_set_t = std::vector<por::event::event const*>;

	struct leaf {
		node* start;
		std::deque<por::event::event const*> catch_up;
	};

	template<typename V>
	class node_branch_iterator {
	public:
		using value_type = V;
		using difference_type = std::ptrdiff_t;
		using pointer = value_type const*;
		using reference = value_type const&;

	private:
		value_type _branch = nullptr;
		value_type _node = nullptr;

	public:
		using iterator_category = std::forward_iterator_tag;

		node_branch_iterator() = default;
		explicit node_branch_iterator(std::remove_pointer_t<value_type>& branch, bool end=false) : _branch(&branch) {
			if(!end) {
				_node = &branch;
			}
		}

		reference operator*() const noexcept { return _node; }
		pointer operator->() const noexcept { return &_node; }

		node_branch_iterator& operator++() noexcept {
			while(_node && _node->is_right_child()) {
				// skip parent of right child
				_node = _node->parent();
			}

			if(_node) {
				_node = _node->parent();
			}

			return *this;
		}
		node_branch_iterator operator++(int) noexcept {
			node_branch_iterator tmp = *this;
			++(*this);
			return tmp;
		}

		bool operator==(const node_branch_iterator& rhs) const noexcept {
			return _branch == rhs._branch && _node == rhs._node;
		}
		bool operator!=(const node_branch_iterator& rhs) const noexcept {
			return !(*this == rhs);
		}
	};

	class node {
		class passkey {
			passkey() {}
			friend node;
		};

		node* _parent = nullptr;
		por::event::event const* _event = nullptr;

		// IMPORTANT: configuration and standby state always need to correspond to each other
		std::shared_ptr<por::configuration> _C; // right node has same configuration
		std::shared_ptr<state const> _standby_state;

		event_set_t _D;
		std::unique_ptr<node> _left, _right;
		bool _is_sweep_node = false;

		std::unique_ptr<node> allocate_left_child() {
			if(_C) {
				return std::make_unique<node>(passkey{}, this, std::make_shared<por::configuration>(*_C), _D);
			} else {
				return std::make_unique<node>(passkey{}, this, _D);
			}
		}

		std::unique_ptr<node> allocate_right_child(event_set_t D) {
			return std::make_unique<node>(passkey{}, this, _C, D);
		}

	public:
		node(passkey, node* parent, std::shared_ptr<por::configuration> C, event_set_t D)
		: _parent(parent), _C(std::move(C)), _D(D) { }

		node(passkey, node* parent, event_set_t D)
		: _parent(parent), _D(D) { }

		// root constructor
		explicit node() : _C(std::make_shared<por::configuration>()), _D({}), _is_sweep_node(true) { }

		node(const node&) = delete;
		node(node&&) = delete;
		node& operator=(const node&) = delete;
		node& operator=(node&&) = delete;
		~node() {
			// avoid infinite recursion: use post-order tree traversal
			std::stack<node*> S;
			node* n = this;
			node* last = nullptr;
			while(!S.empty() || n) {
				if(n) {
					S.push(n);
					n = n->left_child();
				} else {
					node* c = S.top();
					if(c->right_child() && last != c->right_child()) {
						n = c->right_child();
					} else {
						node *p = c->parent();
						assert(!c->has_children());
						if(p && c->is_right_child()) {
							p->_right.reset();
						} else if(p && c->is_left_child()) {
							p->_left.reset();
						}
						last = c;
						S.pop();
					}
				}
			}
		}

		por::configuration const& configuration() const noexcept {
			assert(_C);
			return *_C.get();
		}

		event_set_t const& D() const noexcept {
			return _D;
		}

		por::event::event const* event() const noexcept {
			return _event;
		}

		node* parent() noexcept {
			libpor_check(!_parent || _parent->_left.get() == this || _parent->_right.get() == this);
			return _parent;
		}

		node const* parent() const noexcept {
			libpor_check(!_parent || _parent->_left.get() == this || _parent->_right.get() == this);
			return _parent;
		}

		bool has_children() const noexcept {
			return _left || _right;
		}

		node* left_child() noexcept {
			libpor_check(!_left || _left->_parent == this);
			return _left.get();
		}

		node const* left_child() const noexcept {
			libpor_check(!_left || _left->_parent == this);
			return _left.get();
		}

		node* right_child() noexcept {
			libpor_check(!_right || _right->_parent == this);
			return _right.get();
		}

		node const* right_child() const noexcept {
			libpor_check(!_right || _right->_parent == this);
			return _right.get();
		}

		bool is_left_child() const noexcept {
			return !parent() || parent()->left_child() == this;
		}

		bool is_right_child() const noexcept {
			return !parent() || parent()->right_child() == this;
		}

		bool has_event() const noexcept {
			return _event != nullptr;
		}

		state const* standby_state() const noexcept {
			return _standby_state.get();
		}

		using registration_t = std::pair<por::event::event const*, std::shared_ptr<por::state const>>;

		node* make_left_child(std::function<registration_t(por::configuration&)>);

		node* make_right_local_child(std::function<registration_t(por::configuration&)>);

		static std::vector<leaf> create_right_branches(std::vector<node*>);

		node* catch_up(std::function<registration_t(por::configuration&)>, por::event::event const*);

		void backtrack();

		auto branch_begin() const noexcept { return node_branch_iterator<node const*>(*this); }
		auto branch_end() const noexcept { return node_branch_iterator<node const*>(*this, true); }

		auto branch_cbegin() const noexcept { return node_branch_iterator<node const*>(*this); }
		auto branch_cend() const noexcept { return node_branch_iterator<node const*>(*this, true); }

		auto branch_begin() noexcept { return node_branch_iterator<node*>(*this); }
		auto branch_end() noexcept { return node_branch_iterator<node*>(*this, true); }

		auto branch() noexcept { return util::make_iterator_range(branch_begin(), branch_end()); }
		auto branch() const noexcept { return util::make_iterator_range(branch_begin(), branch_end()); }
		auto cbranch() const noexcept { return util::make_iterator_range(branch_begin(), branch_end()); }

		por::event::event const* last_included_event() const noexcept {
			if(!_parent) {
				// root node
				return _event;
			}

			auto it = branch_begin();
			assert(it != branch_end());
			++it;
			return (*it)->_event;
		}

		std::vector<por::event::event const*> rschedule() const noexcept {
			std::vector<por::event::event const*> sched;
			auto it = branch_begin();
			auto ie = branch_end();
			if(it != ie) {
				assert(*it == this); // we want to skip the current node
				for(++it; it != ie; ++it) {
					node const* n = *it;
					assert(n != this);
					assert(n->has_event());
					sched.push_back(n->_event);
				}
			}
			sched.push_back(&_C->unfolding()->root());
			return sched;
		}
		std::vector<por::event::event const*> schedule() const noexcept {
			auto sched = rschedule();
			std::reverse(sched.begin(), sched.end());
			return sched;
		}

		std::size_t distance_to_last_standby_state() const noexcept {
			std::size_t result = 0;
			node const* n = this;
			while(n->parent() && !n->_standby_state) {
				++result;
				n = n->parent();
			}
			return result;
		}

		std::string to_string(bool with_schedule=true) const noexcept;

	private:
		node* make_right_child();

		leaf make_right_branch(por::comb);

		void update_sweep_bit();
	};
}
