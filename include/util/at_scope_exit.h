#pragma once

#include <type_traits>

namespace util {
	template<typename F>
	struct at_scope_exit_t : private F {
		at_scope_exit_t(F func) : F(std::move(func)) { }

		~at_scope_exit_t() noexcept(noexcept(static_cast<F&>(*this)())) {
			static_cast<F&>(*this)();
		}
	};

	template<typename F>
	auto make_at_scope_exit(F func) {
		return at_scope_exit_t<std::decay_t<F>>(std::move(func));
	}
}
