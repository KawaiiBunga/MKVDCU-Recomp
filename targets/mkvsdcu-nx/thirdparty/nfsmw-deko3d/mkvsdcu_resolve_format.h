#pragma once
#include <cstdint>

namespace nfsmw::nativo {
// GetResolveInfo's format validation: a non-resolvable destination code uses
// the source color surface's texture format. In particular, format zero is
// not a one-bit destination for a color resolve.
constexpr uint32_t ResolveDestinationFormat(uint32_t requested, uint32_t source) {
  switch (requested) {
    case 2: case 3: case 4: case 5: case 6: case 7: case 8: case 9: case 10:
    case 14: case 15: case 16: case 17:
    case 24: case 25: case 26: case 30: case 31: case 32:
    case 36: case 37: case 38: case 50: case 54: case 55: case 56:
      return requested;
    default: break;
  }
  switch (source) {
    case 2: case 10: return 7;
    case 4: return 25;
    case 5: return 26;
    case 6: return 31;
    case 7: return 32;
    case 14: return 36;
    case 15: return 37;
    default: return 6;
  }
}
}
