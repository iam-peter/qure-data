// This project's own effect: a CRT look – scanlines, a slight curve and a
// vignette. p0 strength (0..1). Edit and save while it runs.
vec4 effect(vec2 uv) {
    vec2 c = uv - 0.5;
    vec2 bent = c * (1.0 + 0.08 * p0 * dot(c, c)) + 0.5;
    vec4 col = texture(source0, bent);
    float scan = 0.85 + 0.15 * sin(bent.y * resolution.y * 3.14159);
    float vignette = 1.0 - p0 * 1.2 * dot(c, c);
    float inside = step(0.0, bent.x) * step(bent.x, 1.0) * step(0.0, bent.y) * step(bent.y, 1.0);
    return vec4(col.rgb * mix(1.0, scan, p0) * vignette * inside, 1.0);
}
