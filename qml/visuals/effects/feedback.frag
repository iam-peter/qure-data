// feedback: the new frame (source0) over the previous one (source1),
// zoomed by p1 and turned by p2 around the centre, faded by p0.
vec4 effect(vec2 uv) {
    vec2 p = uv - 0.5;
    float a = p2;
    p = mat2(cos(a), -sin(a), sin(a), cos(a)) * p / max(p1, 0.001);
    vec4 old = texture(source1, p + 0.5) * p0;
    vec4 cur = texture(source0, uv);
    return max(cur, old);
}
