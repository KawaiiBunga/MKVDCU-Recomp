#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace mkvsdcu {
// Versioned PS cache bit; existing shader bytes and keys remain unchanged.
inline constexpr uint32_t kLogicalFragCoordV1 = 0x100;
inline float SceneScale(uint32_t pitch, int percent) {
  // MKvDCU's scene is 1040x624. The 1280x720 composition/UI and small
  // shadow/bloom/exposure targets retain their original resolution.
  return pitch == 1040 ? float(std::clamp(percent, 50, 100)) * .01f : 1.f;
}
template<class Rect> Rect ScaleRect(Rect r, float scale) {
  if (scale == 1.f) return r;
  const auto x1 = uint32_t(std::ceil(double(r.x + r.width) * scale));
  const auto y1 = uint32_t(std::ceil(double(r.y + r.height) * scale));
  r.x = uint32_t(std::floor(double(r.x) * scale));
  r.y = uint32_t(std::floor(double(r.y) * scale));
  r.width = x1 - r.x; r.height = y1 - r.y;
  return r;
}
inline void ScaleViewport(float* v, float scale) {
  for (int i = 0; i < 4; ++i) v[i] *= scale;
}
inline void LogicalFragCoord(std::string& glsl) {
  // Word 242 is reserved in the fixed v4 ABI. Only these new variant keys
  // consume it, so all previously compiled programs stay valid.
  const std::string token = "gl_FragCoord";
  const std::string expression = "vec4(gl_FragCoord.xy * g_UboCompartidas.v[60].z, gl_FragCoord.zw)";
  size_t pos = 0;
  while ((pos = glsl.find(token, pos)) != std::string::npos) {
    glsl.replace(pos, token.size(), expression);
    pos += expression.size();
  }
}
}  // namespace mkvsdcu
