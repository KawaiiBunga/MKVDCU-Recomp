#version 460
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;
void main() {
    // D3D clip-space Y points upward; upper-left origin puts positive Y at top.
    if (uv.y > 0.5)
        color = uv.x < 0.5 ? vec4(1, 0, 0, 1) : vec4(0, 1, 0, 1);
    else
        color = uv.x < 0.5 ? vec4(0, 0, 1, 1) : vec4(1, 1, 1, 1);
}
