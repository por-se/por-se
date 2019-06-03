#pragma once

#include "base.h"

#include <memory>

namespace por::event {
	class program_init final : public event {
		// program init has no predecessors

	protected:
		program_init()
			: event(event_kind::program_init, {})
		{ }

	public:
		static std::shared_ptr<program_init> alloc() {
			// no need for deduplication: program_init is unique per unfolding
			return std::make_shared<program_init>(program_init{});
		}

		virtual std::string to_string(bool details) const override {
			if(details)
				return "[tid: " + tid().to_string() + " depth: " + std::to_string(depth()) + " kind: program_init]";
			return "program_init";
		}

		virtual util::iterator_range<std::shared_ptr<event>*> predecessors() override {
			return util::make_iterator_range<std::shared_ptr<event>*>(nullptr, nullptr);
		}

		virtual util::iterator_range<std::shared_ptr<event> const*> predecessors() const override {
			return util::make_iterator_range<std::shared_ptr<event> const*>(nullptr, nullptr);
		}

	};
}
