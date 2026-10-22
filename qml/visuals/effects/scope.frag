// scope: draws the waveform of source0 – connect [gfx.audio~] – as a
// glowing line. p0 thickness (1), p1 gain (1), p2 hue.
vec4 effect(vec2 uv) {
    float gain = p1 == 0.0 ? 1.0 : p1;
    float v = (texture(source0, vec2(uv.x, 0.5)).r * 2.0 - 1.0) * gain;
    float d = abs(uv.y - (0.5 - v * 0.45));
    float w = (p0 == 0.0 ? 1.0 : p0) * 3.0 / resolution.y;
    float glow = w / (d + w) * 0.9 + smoothstep(w, 0.0, d);
    vec3 c = 0.5 + 0.5 * cos(6.2832 * (p2 + vec3(0.55, 0.65, 0.85)));
    return vec4(c * glow, 1.0);
}
