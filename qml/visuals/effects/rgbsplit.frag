// rgbsplit: shifts the colour channels of source0 apart by p0 (0.01).
vec4 effect(vec2 uv) {
    float d = p0 == 0.0 ? 0.01 : p0;
    vec2 dir = vec2(cos(time), sin(time)) * d;
    float r = texture(source0, uv + dir).r;
    float g = texture(source0, uv).g;
    float b = texture(source0, uv - dir).b;
    return vec4(r, g, b, 1.0);
}
