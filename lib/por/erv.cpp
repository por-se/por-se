#include "por/erv.h"

#include "por/comb.h"
#include "por/event/event.h"

#include <algorithm>
#include <functional>
#include <vector>

namespace {
	template<typename InputIt1, typename InputIt2, typename Comp>
	int lex_compare(InputIt1 first1, InputIt1 last1, InputIt2 first2, InputIt2 last2, Comp comp) {
		for( ; (first1 != last1) && (first2 != last2); ++first1, (void) ++first2 ) {
			if(int res = comp(*first1, *first2); res != 0) return res;
		}
		if(first1 == last1) {
			if(first2 == last2) {
				return 0;
			} else {
				return -1;
			}
		} else {
			if(first2 == last2) {
				return 1;
			} else {
				std::abort();
			}
		}
	}

	// < 0: a < b => -1
	// = 0: a == b
	// > 0: a > b => 1
	int comp_event_total_order(por::event::event const* a, por::event::event const* b) {
		if(a == b) {
			return 0;
		} else if(a->depth() != b->depth()) {
			return a->depth() < b->depth() ? -1 : 1;
		} else if(a->kind() != b->kind()) {
			return a->kind() < b->kind() ? -1 : 1;
		} else if(a->lid() != b->lid()) {
			return a->lid() < b->lid() ? -1 : 1;
		} else if(a->cid() != b->cid()) {
			return a->cid() < b->cid() ? -1 : 1;
		} else if(a->tid() != b->tid()) {
			return a->tid() < b->tid() ? -1 : 1;
		} else if(a->predecessors().size() != a->predecessors().size()) {
			return a->predecessors().size() < b->predecessors().size() ? -1 : 1;
		} else {
			auto aimm = a->immediate_predecessors();
			auto bimm = b->immediate_predecessors();
			int res = lex_compare(
				aimm.begin(), aimm.end(),
				bimm.begin(), bimm.end(),
				comp_event_total_order);
			if(res != 0) {
				return res;
			}
		}
		// memory address as tie breaker: total order
		return a < b ? -1 : 1; // we already checked a == b
	}

	class parikh_vector {
		std::vector<por::event::event const*> vector;

	public:
		template<typename T>
		parikh_vector(T begin, T end) : vector(begin, end) {
			std::sort(vector.begin(), vector.end(), [](auto a, auto b) {
				return comp_event_total_order(a, b) < 0;
			});
		}

		parikh_vector(std::vector<por::event::event const*>&& v) : vector(std::move(v)) {
			std::sort(vector.begin(), vector.end(), [](auto a, auto b) {
				return comp_event_total_order(a, b) < 0;
			});
		}

		parikh_vector(por::event::event const& lc)
		: parikh_vector(lc.local_configuration_begin(), lc.local_configuration_end()) { }

		auto begin() const noexcept { return vector.cbegin(); }
		auto end() const noexcept { return vector.cend(); }
		std::size_t size() const noexcept { return vector.size(); }

		friend int compare(parikh_vector const& lhs, parikh_vector const& rhs) noexcept {
			return lex_compare(
				lhs.begin(), lhs.end(),
				rhs.begin(), rhs.end(),
				comp_event_total_order);
		}
	};

	class foata_normal_form {
		std::vector<parikh_vector> fnf;

	public:
		foata_normal_form(parikh_vector const& pv) {
			por::comb C(pv.begin(), pv.end());
			assert(C.is_sorted());

			fnf.reserve(pv.size());

			while(!C.empty()) {
				fnf.emplace_back(C.min());
				C.remove(fnf.back().begin(), fnf.back().end());
			}
		}

		bool operator<(foata_normal_form const& rhs) const noexcept {
			std::size_t num = std::min(fnf.size(), rhs.fnf.size());
			for(std::size_t i = 0; i < num; ++i) {
				parikh_vector const& C1j = fnf[i];
				parikh_vector const& C2j = rhs.fnf[i];
				if(int res = compare(C1j, C2j); res != 0) {
					return res < 0;
				}
			}
			return false;
		}

		std::string to_string() const noexcept {
			std::stringstream ss;
			for(auto const& set : fnf) {
				ss << "{";
				std::size_t remaining = set.size();
				std::size_t depth = 0;
				for(auto const* event : set) {
					if(!depth) {
						depth = event->depth();
					} else {
						assert(depth == event->depth());
					}
					ss << event->to_string(true) << (--remaining ? ", " : "");
				}
				ss << "}\n";
			}
			return ss.str();
		}
	};
}

namespace por {
	bool compare_adequate_total_order(por::event::event const& a, por::event::event const& b) {
		std::size_t asize = a.local_configuration_size();
		std::size_t bsize = b.local_configuration_size();
		if(asize == bsize) {
			parikh_vector apv(a), bpv(b);
			if(int res = compare(apv, bpv); res == 0) {
				foata_normal_form afnf(a), bfnf(b);
				if(afnf < bfnf) {
					return true;
				} else {
					return false;
				}
			} else if(res < 0) {
				return true;
			} else {
				return false;
			}
		} else if(asize < bsize) {
			return true;
		} else {
			return false;
		}
	}
}
