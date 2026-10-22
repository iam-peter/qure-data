// invert: source0 inverted, mixed by p0 (1 = fully).
vec4 effect(vec2 uv) {
    vec4 c = texture(source0, uv);
    float amount = p0 == 0.0 ? 1.0 : p0;
    return vec4(mix(c.rgb, 1.0 - c.rgb, amount), c.a);
}
