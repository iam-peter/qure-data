#version 440

layout(location = 0) in vec4 vColor;
layout(location = 1) in float vDist;
layout(location = 2) in float vHalfWidth;

layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float scale;
};

void main() {
    float alpha = clamp(vHalfWidth + 0.5 - abs(vDist), 0.0, 1.0);
    fragColor = vColor * alpha * qt_Opacity;
}
