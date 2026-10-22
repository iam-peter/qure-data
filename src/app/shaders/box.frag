#version 440
// Signed-distance rendering of Pd's box shapes: plain boxes, message boxes
// (flag on the right), atom boxes (cut corner) and broken boxes (dashed).

layout(location = 0) in vec2 vLocal;
layout(location = 1) in vec2 vHalf;
layout(location = 2) in vec4 vFill;
layout(location = 3) in vec4 vStroke;
layout(location = 4) in vec4 vParams;

layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float scale;
};

float roundRect(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + vec2(r);
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

void main() {
    float radius = vParams.x;
    float borderPx = vParams.y;
    int shape = int(vParams.z + 0.5);
    float notch = vParams.w;

    float d = roundRect(vLocal, vHalf, radius);
    if (shape == 1) {
        // Message box: the right edge is indented like a flag.
        float inset = notch * clamp((vHalf.y - abs(vLocal.y)) / notch, 0.0, 1.0);
        d = max(d, (vLocal.x - (vHalf.x - inset)) * 0.7071);
    } else if (shape == 3) {
        // Atom box: top-right corner cut off.
        d = max(d, (vLocal.x - vLocal.y - (vHalf.x + vHalf.y - notch)) * 0.7071);
    }

    float px = d * scale;
    float coverage = clamp(0.5 - px, 0.0, 1.0);
    float borderMix = borderPx > 0.0 ? clamp(px + borderPx + 0.5, 0.0, 1.0) : 0.0;
    if (shape == 2) {
        // Broken object: dashed outline.
        float t = (vLocal.x - vLocal.y) * scale / 5.0;
        borderMix *= step(0.5, fract(t * 0.5));
    }
    fragColor = mix(vFill, vStroke, borderMix) * coverage * qt_Opacity;
}
