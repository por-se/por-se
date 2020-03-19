#pragma once

#include "por/cone.h"

namespace por::event {
	class event;

	class event_iterator {
		event const* _lc = nullptr;
		por::cone::const_reverse_iterator _thread;
		event const* _event = nullptr;
		bool _with_root = true; // include program_init event

	public:
		using value_type = event const*;
		using difference_type = std::ptrdiff_t;
		using pointer = event const* const*;
		using reference = event const* const&;

		using iterator_category = std::forward_iterator_tag;

		event_iterator() = default;

		// this iterator supports different modes:
		// with_root =  true, with_event =  true => [e] (local configuration of e)
		// with_root = false, with_event =  true => [e] \ {program_init} (local configuration without root event)
		// with_root =  true, with_event = false => ⌈e⌉ := [e] \ {e} (causes of e)
		// with_root = false, with_event = false => ⌈e⌉ \ {program_init} (causes of e without root event)
		explicit event_iterator(event const& event, bool with_root=true, bool with_event=true, bool end=false);

		reference operator*() const noexcept { return _event; }
		pointer operator->() const noexcept { return &_event; }

		event_iterator& operator++() noexcept;
		event_iterator operator++(int) noexcept {
			event_iterator tmp = *this;
			++(*this);
			return tmp;
		}

		bool operator==(const event_iterator& rhs) const noexcept {
			libpor_check(decltype(_thread)() == decltype(_thread)());
			return _lc == rhs._lc && _thread == rhs._thread && _event == rhs._event && _with_root == rhs._with_root;
		}
		bool operator!=(const event_iterator& rhs) const noexcept {
			return !(*this == rhs);
		}
	};
}
