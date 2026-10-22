#version 440
// One quad per box/iolet. The quad is expanded by an anti-aliasing margin
// in *screen* space so edges stay crisp at every zoom level.

layout(location = 0) in vec2 center;      // canvas units
layout(location = 1) in vec2 corner;      // (-1|1, -1|1)
layout(location = 2) in vec2 halfSize;    // canvas units
layout(location = 3) in vec4 fillColor;   // premultiplied
layout(location = 4) in vec4 strokeColor; // premultiplied
layout(location = 5) in vec4 params;      // radius (units), border (px), shape, notch (units)

layout(location = 0) out vec2 vLocal;
layout(location = 1) out vec2 vHalf;
layout(location = 2) out vec4 vFill;
layout(location = 3) out vec4 vStroke;
layout(location = 4) out vec4 vParams;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float scale; // device pixels per canvas unit
};

void main() {
    vec2 margin = vec2(1.5 / scale);
    vec2 local = corner * (halfSize + margin);
    vLocal = local;
    vHalf = halfSize;
    vFill = fillColor;
    vStroke = strokeColor;
    vParams = params;
    gl_Position = qt_Matrix * vec4(center + local, 0.0, 1.0);
}
