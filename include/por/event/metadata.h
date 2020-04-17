#pragma once

#include "klee/Fingerprint/MemoryFingerprintDelta.h"
#include "klee/Fingerprint/MemoryFingerprintValue.h"

#include "por/thread_id.h"

#include <cstddef>
#include <map>
#include <vector>

namespace klee {
	class MemoryObject;
}

namespace por {
	namespace event {
		struct metadata {
			klee::MemoryFingerprintValue fingerprint;
			klee::MemoryFingerprintDelta thread_delta;
			std::map<por::thread_id, std::vector<const klee::MemoryObject*>> pending_frees;
			std::size_t id;
		};

		inline bool operator==(metadata const& a, metadata const& b) noexcept {
			return a.fingerprint == b.fingerprint && a.thread_delta == b.thread_delta;
		}
	}
}
