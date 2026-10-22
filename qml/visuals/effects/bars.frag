// bars: source0's level and waveform as a row of bars. p0 count (32).
vec4 effect(vec2 uv) {
    float n = p0 == 0.0 ? 32.0 : p0;
    float i = floor(uv.x * n);
    float v = abs(texture(source0, vec2((i + 0.5) / n, 0.5)).r * 2.0 - 1.0);
    float level = texture(source0, vec2(0.5, 0.5)).g;
    float h = clamp(v * 1.5 + level * 0.5, 0.0, 1.0);
    float inBar = step(1.0 - h, uv.y) * step(0.12, fract(uv.x * n));
    vec3 c = mix(vec3(0.1, 0.8, 0.7), vec3(0.9, 0.3, 0.6), uv.y);
    return vec4(c * inBar, 1.0);
}
