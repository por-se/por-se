#pragma once

#include "base.h"

namespace por::event {
	class program_init final : public event {
		// program init has no predecessors

	protected:
		friend class por::unfolding;
		program_init()
			: event(event_kind::program_init, {})
		{ }

	public:
		program_init(const program_init&) = delete;
		program_init(program_init&&) = default;
		program_init& operator=(const program_init&) = delete;
		program_init& operator=(program_init&&) = delete;
		~program_init() = default;

		std::string to_string(bool details) const noexcept override {
			if(details)
				return "[tid: " + tid().to_string() + " depth: " + std::to_string(depth()) + " kind: program_init]";
			return "program_init";
		}

		event const* thread_predecessor() const noexcept override {
			return nullptr;
		}

	};
}
