// kaleido: mirrors source0 into p0 segments (6), turning with p1 (0.1).
vec4 effect(vec2 uv) {
    float n = p0 == 0.0 ? 6.0 : p0;
    vec2 p = uv - 0.5;
    float a = atan(p.y, p.x) + time * p1;
    float r = length(p);
    float seg = 6.2832 / n;
    a = abs(mod(a, seg) - seg * 0.5);
    vec2 q = vec2(cos(a), sin(a)) * r + 0.5;
    return texture(source0, q);
}
