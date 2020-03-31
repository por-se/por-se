#pragma once

#include "klee/Fingerprint/MemoryFingerprintDelta.h"
#include "klee/Fingerprint/MemoryFingerprintValue.h"

namespace por {
	namespace event {
		struct metadata {
			klee::MemoryFingerprintValue fingerprint;
			klee::MemoryFingerprintDelta thread_delta;
		};

		inline bool operator==(metadata const& a, metadata const& b) noexcept {
			return a.fingerprint == b.fingerprint && a.thread_delta == b.thread_delta;
		}
	}
}
