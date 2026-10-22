// Xenos rectangle lists: three original corners, one extrapolated corner.
// The diagonal / cyclic ordering follows ReXGlue/Xenia's rectangle geometry
// shader (BSD, see the SDK LICENSE). Only live pixel interpolators are emitted.
#pragma once
#include <cstdint>
#include <string>

namespace nfsmw::deko {
inline std::string RectangleGeometryGlsl(uint32_t mask) {
  std::string s="#version 460\nlayout(triangles) in;\nlayout(triangle_strip,max_vertices=4) out;\n";
  for(uint32_t i=0;i<32;++i) if(mask&(1u<<i)) {
    const auto n=std::to_string(i);
    s+="layout(location="+n+") in vec4 v"+n+"[];\nlayout(location="+n+") out vec4 o"+n+";\n";
  }
  s+=R"(
void main() {
  vec2 d12=gl_in[2].gl_Position.xy-gl_in[1].gl_Position.xy;
  vec2 d20=gl_in[0].gl_Position.xy-gl_in[2].gl_Position.xy;
  vec2 d01=gl_in[1].gl_Position.xy-gl_in[0].gl_Position.xy;
  vec3 length2=vec3(dot(d12,d12),dot(d20,d20),dot(d01,d01));
  int a=length2.x>length2.y && length2.x>length2.z ? 0 : (length2.y>length2.z ? 1 : 2);
  int b=(a+1)%3, c=(a+2)%3;
  for(int vertex=0;vertex<4;++vertex) {
    int source=(a+vertex)%3;
    gl_Position=vertex<3 ? gl_in[source].gl_Position :
      -gl_in[a].gl_Position+gl_in[b].gl_Position+gl_in[c].gl_Position;
)";
  for(uint32_t i=0;i<32;++i) if(mask&(1u<<i)) {
    const auto n=std::to_string(i);
    s+="o"+n+"=vertex<3 ? v"+n+"[source] : -v"+n+"[a]+v"+n+"[b]+v"+n+"[c];\n";
  }
  s+="EmitVertex();\n}\nEndPrimitive();\n}\n";
  return s;
}
}
