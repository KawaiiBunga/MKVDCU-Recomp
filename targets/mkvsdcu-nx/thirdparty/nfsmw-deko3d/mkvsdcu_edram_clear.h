#pragma once
#include <algorithm>
#include <cstdint>

namespace nfsmw::nativo {
struct EdramSurface {
  uint32_t base, pitch, samples;
};
struct EdramClearRect {
  uint32_t x0, y0, x1, y1;
};
// Xenos tiles contain 80x16 samples. 2x MSAA doubles Y; 4x doubles
// both axes. Clear rectangles alias in sample space, not pixel space.
constexpr uint32_t EdramPitchTiles(EdramSurface s) {
  return ((s.pitch << uint32_t(s.samples >= 2)) + 79) / 80;
}
constexpr bool MapEdramClear(EdramSurface source, EdramSurface target,
                             EdramClearRect rectangle, EdramClearRect& mapped) {
  if (!source.pitch || !target.pitch || source.samples > 2 || target.samples > 2 ||
      rectangle.x0>=rectangle.x1 || rectangle.y0>=rectangle.y1 || rectangle.x1>source.pitch ||
      (source.base & 2047) != (target.base & 2047) ||
      EdramPitchTiles(source) != EdramPitchTiles(target)) return false;
  const uint32_t sx = uint32_t(source.samples >= 2), sy = uint32_t(source.samples >= 1);
  const uint32_t tx = uint32_t(target.samples >= 2), ty = uint32_t(target.samples >= 1);
  rectangle = {rectangle.x0 << sx, rectangle.y0 << sy,
               rectangle.x1 << sx, rectangle.y1 << sy};
  // A uniform clear transfers only if it covers complete target pixels.
  if (((rectangle.x0 | rectangle.x1) & ((1u << tx)-1)) ||
      ((rectangle.y0 | rectangle.y1) & ((1u << ty)-1))) return false;
  mapped = {rectangle.x0 >> tx, rectangle.y0 >> ty,
            rectangle.x1 >> tx, rectangle.y1 >> ty};
  mapped.x1 = std::min(mapped.x1, target.pitch);
  return mapped.x1 > mapped.x0 && mapped.y1 > mapped.y0;
}
}  // namespace nfsmw::nativo
