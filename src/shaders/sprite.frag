#version 450
layout(binding=0) uniform sampler2D tex;
layout(location=0) in vec2 vUV;
layout(location=1) in vec4 vTint;
layout(location=2) in flat int vSolid;   // 1 => draw flat color, ignore texture
layout(location=0) out vec4 outColor;
void main() {
    if (vSolid == 1) {
        // solid-color quad (e.g. focus splash, bars, panel bg): output tint only.
        // alpha is vTint.a (already premultiplied-style; blend uses srcAlpha).
        if (vTint.a < 0.004) discard;
        outColor = vTint;
        return;
    }
    vec4 c = texture(tex, vUV);
    if (c.a < 0.05) discard;
    outColor = vec4(c.rgb * vTint.rgb, c.a * vTint.a);
}
