#pragma once
#include <cstdint>

namespace mkvsdcu {
// A separate cache namespace for the verified, forward-only dispatcher rewrite.
// The ordinary compiler identity, shader library and variants remain usable.
inline constexpr uint64_t kAcyclicVertexFingerprint = 0x436F5A41F92DD8E7ull;
inline constexpr uint32_t kAcyclicVertexV1 = 0x80000000u;
inline bool AcyclicVertexEligible(bool vertex,uint64_t fingerprint,uint32_t specialization) {
  return vertex && fingerprint==kAcyclicVertexFingerprint && specialization==0;
}
}
