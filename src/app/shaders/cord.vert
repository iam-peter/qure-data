#version 440
// Patch cords as screen-space anti-aliased line quads: the quad is widened
// in the vertex shader, so any number of cords is a single draw call and
// line width stays exact while zooming.

layout(location = 0) in vec2 p0;      // canvas units
layout(location = 1) in vec2 p1;
layout(location = 2) in vec2 tv;      // x: 0 at p0, 1 at p1; y: side -1|1
layout(location = 3) in vec4 color;   // premultiplied
layout(location = 4) in float width;  // canvas units (at least one pixel is drawn)

layout(location = 0) out vec4 vColor;
layout(location = 1) out float vDist;
layout(location = 2) out float vHalfWidth;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float scale;
};

void main() {
    float wpx = max(width * scale, 1.0);
    vec2 d = p1 - p0;
    float len = length(d);
    vec2 dir = len > 0.0001 ? d / len : vec2(1.0, 0.0);
    vec2 n = vec2(-dir.y, dir.x);
    float halfPx = wpx * 0.5 + 1.0;
    vec2 pos = mix(p0, p1, tv.x) + n * (tv.y * halfPx / scale) + dir * ((tv.x * 2.0 - 1.0) * 0.5 / scale);
    vDist = tv.y * halfPx;
    vHalfWidth = wpx * 0.5;
    vColor = color;
    gl_Position = qt_Matrix * vec4(pos, 0.0, 1.0);
}
