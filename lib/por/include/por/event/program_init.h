#pragma once

#include "base.h"

namespace por::event {
	class program_init final : public event {
		// program init has no predecessors

	protected:
		program_init()
		: event(event_kind::program_init, 0)
		{ }

	public:
		static std::shared_ptr<program_init> alloc() {
			return std::make_shared<program_init>(program_init{});
		}

		virtual util::iterator_range<std::shared_ptr<event>*> predecessors() override {
			return util::make_iterator_range<std::shared_ptr<event>*>(nullptr, nullptr);
		}
	};
}
