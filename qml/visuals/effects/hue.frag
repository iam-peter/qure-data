// hue: rotates the hue of source0 by p0 turns (0..1).
vec4 effect(vec2 uv) {
    vec4 c = texture(source0, uv);
    float a = p0 * 6.2832;
    vec3 k = vec3(0.57735);
    vec3 rot = c.rgb * cos(a) + cross(k, c.rgb) * sin(a) + k * dot(k, c.rgb) * (1.0 - cos(a));
    return vec4(rot, c.a);
}
