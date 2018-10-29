#pragma once

#include "base.h"

namespace por::event {
	class program_start final : public event {
		// program start has no predecessors

	protected:
		program_start()
		: event(event_kind::program_start, 0)
		{ }

	public:
		static std::shared_ptr<program_start> alloc() {
			return std::make_shared<program_start>(program_start{});
		}

		virtual util::iterator_range<std::shared_ptr<event>*> predecessors() override {
			return util::make_iterator_range<std::shared_ptr<event>*>(nullptr, nullptr);
		}
	};
}
