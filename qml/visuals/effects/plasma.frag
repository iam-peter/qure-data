// plasma: a moving colour field. p0 speed (1), p1 scale (1), p2 hue shift.
vec4 effect(vec2 uv) {
    float speed = p0 == 0.0 ? 1.0 : p0;
    float scale = p1 == 0.0 ? 1.0 : p1;
    vec2 p = (uv - 0.5) * vec2(resolution.x / resolution.y, 1.0) * 6.0 * scale;
    float t = time * speed;
    float v = sin(p.x + t) + sin(p.y * 1.3 - t * 0.7) + sin(length(p) * 1.5 - t * 1.2);
    vec3 c = 0.5 + 0.5 * cos(6.2832 * (v * 0.25 + p2 + vec3(0.0, 0.33, 0.67)));
    return vec4(c, 1.0);
}
