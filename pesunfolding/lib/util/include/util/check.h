#pragma once

#if defined(LIBPOR_CHECKED)
#	ifdef NDEBUG
		static_assert(0, "Assertions must be enabled for LIBPOR_CHECKED!");
#	endif
#	define libpor_check(expr) (assert(expr))
#else
#	define libpor_check(expr) ((void)0)
#endif
