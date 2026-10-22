// A project-local effect: edit and save while the patch runs – it is
// recompiled and swapped in live. p0 glow strength, p1 vignette.
//
// Every effect gets: time, resolution, p0..p7, source0, source1.
vec4 effect(vec2 uv) {
    vec4 c = texture(source0, uv);
    // A cheap bloom: average of a few blurred taps, added on top.
    vec2 px = 3.0 / resolution;
    vec4 blur = (texture(source0, uv + vec2(px.x, 0.0)) + texture(source0, uv - vec2(px.x, 0.0)) +
                 texture(source0, uv + vec2(0.0, px.y)) + texture(source0, uv - vec2(0.0, px.y))) * 0.25;
    vec3 col = c.rgb + blur.rgb * p0;
    float vignette = 1.0 - p1 * dot(uv - 0.5, uv - 0.5) * 2.0;
    return vec4(col * vignette, 1.0);
}
