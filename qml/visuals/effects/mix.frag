// mix: crossfade from source0 to source1 by p0 (used by [gfx.mix]).
vec4 effect(vec2 uv) {
    return mix(texture(source0, uv), texture(source1, uv), clamp(p0, 0.0, 1.0));
}
